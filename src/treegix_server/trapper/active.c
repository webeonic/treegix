

#include "common.h"
#include "db.h"
#include "dbcache.h"
#include "log.h"
#include "trxserver.h"
#include "trxregexp.h"

#include "active.h"
#include "../../libs/trxcrypto/tls_tcp_active.h"

extern unsigned char	program_type;

/******************************************************************************
 *                                                                            *
 * Function: db_register_host                                                 *
 *                                                                            *
 * Purpose: perform active agent auto registration                            *
 *                                                                            *
 * Parameters: host          - [IN] name of the host to be added or updated   *
 *             ip            - [IN] IP address of the host                    *
 *             port          - [IN] port of the host                          *
 *             connection_type - [IN] TRX_TCP_SEC_UNENCRYPTED,                *
 *                             TRX_TCP_SEC_TLS_PSK or TRX_TCP_SEC_TLS_CERT    *
 *             host_metadata - [IN] host metadata                             *
 *             flag          - [IN] flag describing interface type            *
 *             interface     - [IN] interface value if flag is not default    *
 *                                                                            *
 * Comments: helper function for get_hostid_by_host                           *
 *                                                                            *
 ******************************************************************************/
static void	db_register_host(const char *host, const char *ip, unsigned short port, unsigned int connection_type,
		const char *host_metadata, trx_conn_flags_t flag, const char *interface)
{
	char		dns[INTERFACE_DNS_LEN_MAX];
	char		ip_addr[INTERFACE_IP_LEN_MAX];
	const char	*p;
	const char	*p_ip, *p_dns;

	p_ip = ip;
	p_dns = dns;

	if (TRX_CONN_DEFAULT == flag)
		p = ip;
	else if (TRX_CONN_IP  == flag)
		p_ip = p = interface;

	trx_alarm_on(CONFIG_TIMEOUT);
	if (TRX_CONN_DEFAULT == flag || TRX_CONN_IP == flag)
	{
		if (0 == strncmp("::ffff:", p, 7) && SUCCEED == is_ip4(p + 7))
			p += 7;

		trx_gethost_by_ip(p, dns, sizeof(dns));
	}
	else if (TRX_CONN_DNS == flag)
	{
		trx_getip_by_host(interface, ip_addr, sizeof(ip_addr));
		p_ip = ip_addr;
		p_dns = interface;
	}
	trx_alarm_off();

	DBbegin();

	if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
	{
		DBregister_host(0, host, p_ip, p_dns, port, connection_type, host_metadata, (unsigned short)flag,
				(int)time(NULL));
	}
	else if (0 != (program_type & TRX_PROGRAM_TYPE_PROXY))
		DBproxy_register_host(host, p_ip, p_dns, port, connection_type, host_metadata, (unsigned short)flag);

	DBcommit();
}

static int	trx_autoreg_check_permissions(const char *host, const char *ip, unsigned short port,
		const trx_socket_t *sock)
{
	trx_config_t	cfg;
	int		ret = FAIL;

	trx_config_get(&cfg, TRX_CONFIG_FLAGS_AUTOREG_TLS_ACCEPT);

	if (0 == (cfg.autoreg_tls_accept & sock->connection_type))
	{
		treegix_log(LOG_LEVEL_WARNING, "autoregistration from \"%s\" denied (host:\"%s\" ip:\"%s\""
				" port:%hu): connection type \"%s\" is not allowed for autoregistration",
				sock->peer, host, ip, port, trx_tcp_connection_type_name(sock->connection_type));
		goto out;
	}

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || (defined(HAVE_OPENSSL) && defined(HAVE_OPENSSL_WITH_PSK))
	if (TRX_TCP_SEC_TLS_PSK == sock->connection_type)
	{
		if (0 == (TRX_PSK_FOR_AUTOREG & trx_tls_get_psk_usage()))
		{
			treegix_log(LOG_LEVEL_WARNING, "autoregistration from \"%s\" denied (host:\"%s\" ip:\"%s\""
					" port:%hu): connection used PSK which is not configured for autoregistration",
					sock->peer, host, ip, port);
			goto out;
		}

		ret = SUCCEED;
	}
	else if (TRX_TCP_SEC_UNENCRYPTED == sock->connection_type)
	{
		ret = SUCCEED;
	}
	else
		THIS_SHOULD_NEVER_HAPPEN;
#else
	ret = SUCCEED;
#endif
out:
	trx_config_clean(&cfg);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: get_hostid_by_host                                               *
 *                                                                            *
 * Purpose: check for host name and return hostid                             *
 *                                                                            *
 * Parameters: host - [IN] require size 'HOST_HOST_LEN_MAX'                   *
 *                                                                            *
 * Return value:  SUCCEED - host is found                                     *
 *                FAIL - an error occurred or host not found                  *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments: NB! adds host to the database if it does not exist or if it      *
 *           exists but metadata has changed                                  *
 *                                                                            *
 ******************************************************************************/
static int	get_hostid_by_host(const trx_socket_t *sock, const char *host, const char *ip, unsigned short port,
		const char *host_metadata, trx_conn_flags_t flag, const char *interface, trx_uint64_t *hostid,
		char *error)
{
	char		*host_esc, *ch_error, *old_metadata, *old_ip, *old_dns, *old_flag, *old_port;
	DB_RESULT	result;
	DB_ROW		row;
	int		ret = FAIL;
	unsigned short	old_port_v;
	int		tls_offset = 0;
	trx_conn_flags_t	old_flag_v;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() host:'%s' metadata:'%s'", __func__, host, host_metadata);

	if (FAIL == trx_check_hostname(host, &ch_error))
	{
		trx_snprintf(error, MAX_STRING_LEN, "invalid host name [%s]: %s", host, ch_error);
		trx_free(ch_error);
		goto out;
	}

	host_esc = DBdyn_escape_string(host);

	result =
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		DBselect(
			"select h.hostid,h.status,h.tls_accept,h.tls_issuer,h.tls_subject,h.tls_psk_identity,"
			"a.host_metadata,a.listen_ip,a.listen_dns,a.listen_port,a.flags"
			" from hosts h"
				" left join autoreg_host a"
					" on a.proxy_hostid is null and a.host=h.host"
			" where h.host='%s'"
				" and h.status in (%d,%d)"
				" and h.flags<>%d"
				" and h.proxy_hostid is null",
			host_esc, HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED, TRX_FLAG_DISCOVERY_PROTOTYPE);
#else
		DBselect(
			"select h.hostid,h.status,h.tls_accept,a.host_metadata,a.listen_ip,a.listen_dns,a.listen_port,"
			"a.flags"
			" from hosts h"
				" left join autoreg_host a"
					" on a.proxy_hostid is null and a.host=h.host"
			" where h.host='%s'"
				" and h.status in (%d,%d)"
				" and h.flags<>%d"
				" and h.proxy_hostid is null",
			host_esc, HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED, TRX_FLAG_DISCOVERY_PROTOTYPE);
#endif
	if (NULL != (row = DBfetch(result)))
	{
		if (0 == ((unsigned int)atoi(row[2]) & sock->connection_type))
		{
			trx_snprintf(error, MAX_STRING_LEN, "connection of type \"%s\" is not allowed for host"
					" \"%s\"", trx_tcp_connection_type_name(sock->connection_type), host);
			goto done;
		}

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		if (TRX_TCP_SEC_TLS_CERT == sock->connection_type)
		{
			trx_tls_conn_attr_t	attr;

			if (SUCCEED != trx_tls_get_attr_cert(sock, &attr))
			{
				THIS_SHOULD_NEVER_HAPPEN;

				trx_snprintf(error, MAX_STRING_LEN, "cannot get connection attributes for host"
						" \"%s\"", host);
				goto done;
			}

			/* simplified match, not compliant with RFC 4517, 4518 */
			if ('\0' != *row[3] && 0 != strcmp(row[3], attr.issuer))
			{
				trx_snprintf(error, MAX_STRING_LEN, "certificate issuer does not match for"
						" host \"%s\"", host);
				goto done;
			}

			/* simplified match, not compliant with RFC 4517, 4518 */
			if ('\0' != *row[4] && 0 != strcmp(row[4], attr.subject))
			{
				trx_snprintf(error, MAX_STRING_LEN, "certificate subject does not match for"
						" host \"%s\"", host);
				goto done;
			}
		}
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || (defined(HAVE_OPENSSL) && defined(HAVE_OPENSSL_WITH_PSK))
		else if (TRX_TCP_SEC_TLS_PSK == sock->connection_type)
		{
			trx_tls_conn_attr_t	attr;

			if (SUCCEED != trx_tls_get_attr_psk(sock, &attr))
			{
				THIS_SHOULD_NEVER_HAPPEN;

				trx_snprintf(error, MAX_STRING_LEN, "cannot get connection attributes for host"
						" \"%s\"", host);
				goto done;
			}

			if (strlen(row[5]) != attr.psk_identity_len ||
					0 != memcmp(row[5], attr.psk_identity, attr.psk_identity_len))
			{
				trx_snprintf(error, MAX_STRING_LEN, "false PSK identity for host \"%s\"", host);
				goto done;
			}
		}
#endif
		tls_offset = 3;
#endif
		old_metadata = row[3 + tls_offset];
		old_ip = row[4 + tls_offset];
		old_dns = row[5 + tls_offset];
		old_port = row[6 + tls_offset];
		old_flag = row[7 + tls_offset];
		old_port_v = (unsigned short)(SUCCEED == DBis_null(old_port)) ? 0 : atoi(old_port);
		old_flag_v = (trx_conn_flags_t)(SUCCEED == DBis_null(old_flag)) ? TRX_CONN_DEFAULT : atoi(old_flag);
		/* metadata is available only on Treegix server */
		if (SUCCEED == DBis_null(old_metadata) || 0 != strcmp(old_metadata, host_metadata) ||
				(TRX_CONN_IP  == flag && ( 0 != strcmp(old_ip, interface)  || old_port_v != port)) ||
				(TRX_CONN_DNS == flag && ( 0 != strcmp(old_dns, interface) || old_port_v != port)) ||
				(old_flag_v != flag))
		{
			db_register_host(host, ip, port, sock->connection_type, host_metadata, flag, interface);
		}

		if (HOST_STATUS_MONITORED != atoi(row[1]))
		{
			trx_snprintf(error, MAX_STRING_LEN, "host [%s] not monitored", host);
			goto done;
		}

		TRX_STR2UINT64(*hostid, row[0]);
		ret = SUCCEED;
	}
	else
	{
		trx_snprintf(error, MAX_STRING_LEN, "host [%s] not found", host);

		if (SUCCEED == trx_autoreg_check_permissions(host, ip, port, sock))
			db_register_host(host, ip, port, sock->connection_type, host_metadata, flag, interface);
	}
done:
	DBfree_result(result);

	trx_free(host_esc);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

static void	get_list_of_active_checks(trx_uint64_t hostid, trx_vector_uint64_t *itemids)
{
	DB_RESULT	result;
	DB_ROW		row;
	trx_uint64_t	itemid;

	result = DBselect(
			"select itemid"
			" from items"
			" where type=%d"
				" and flags<>%d"
				" and hostid=" TRX_FS_UI64,
			ITEM_TYPE_TREEGIX_ACTIVE, TRX_FLAG_DISCOVERY_PROTOTYPE, hostid);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(itemid, row[0]);
		trx_vector_uint64_append(itemids, itemid);
	}
	DBfree_result(result);
}

/******************************************************************************
 *                                                                            *
 * Function: send_list_of_active_checks                                       *
 *                                                                            *
 * Purpose: send list of active checks to the host (older version agent)      *
 *                                                                            *
 * Parameters: sock - open socket of server-agent connection                  *
 *             request - request buffer                                       *
 *                                                                            *
 * Return value:  SUCCEED - list of active checks sent successfully           *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 * Comments: format of the request: TRX_GET_ACTIVE_CHECKS\n<host name>\n      *
 *           format of the list: key:delay:last_log_size                      *
 *                                                                            *
 ******************************************************************************/
int	send_list_of_active_checks(trx_socket_t *sock, char *request)
{
	char			*host = NULL, *p, *buffer = NULL, error[MAX_STRING_LEN];
	size_t			buffer_alloc = 8 * TRX_KIBIBYTE, buffer_offset = 0;
	int			ret = FAIL, i;
	trx_uint64_t		hostid;
	trx_vector_uint64_t	itemids;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (NULL != (host = strchr(request, '\n')))
	{
		host++;
		if (NULL != (p = strchr(host, '\n')))
			*p = '\0';
	}
	else
	{
		trx_snprintf(error, sizeof(error), "host is null");
		goto out;
	}

	/* no host metadata in older versions of agent */
	if (FAIL == get_hostid_by_host(sock, host, sock->peer, TRX_DEFAULT_AGENT_PORT, "", 0, "",  &hostid, error))
		goto out;

	trx_vector_uint64_create(&itemids);

	get_list_of_active_checks(hostid, &itemids);

	buffer = (char *)trx_malloc(buffer, buffer_alloc);

	if (0 != itemids.values_num)
	{
		DC_ITEM		*dc_items;
		int		*errcodes, now;
		trx_config_t	cfg;

		dc_items = (DC_ITEM *)trx_malloc(NULL, sizeof(DC_ITEM) * itemids.values_num);
		errcodes = (int *)trx_malloc(NULL, sizeof(int) * itemids.values_num);

		DCconfig_get_items_by_itemids(dc_items, itemids.values, errcodes, itemids.values_num);
		trx_config_get(&cfg, TRX_CONFIG_FLAGS_REFRESH_UNSUPPORTED);

		now = time(NULL);

		for (i = 0; i < itemids.values_num; i++)
		{
			int	delay;

			if (SUCCEED != errcodes[i])
			{
				treegix_log(LOG_LEVEL_DEBUG, "%s() Item [" TRX_FS_UI64 "] was not found in the"
						" server cache. Not sending now.", __func__, itemids.values[i]);
				continue;
			}

			if (ITEM_STATUS_ACTIVE != dc_items[i].status)
				continue;

			if (HOST_STATUS_MONITORED != dc_items[i].host.status)
				continue;

			if (ITEM_STATE_NOTSUPPORTED == dc_items[i].state)
			{
				if (0 == cfg.refresh_unsupported)
					continue;

				if (dc_items[i].lastclock + cfg.refresh_unsupported > now)
					continue;
			}

			if (SUCCEED != trx_interval_preproc(dc_items[i].delay, &delay, NULL, NULL))
				continue;

			trx_snprintf_alloc(&buffer, &buffer_alloc, &buffer_offset, "%s:%d:" TRX_FS_UI64 "\n",
					dc_items[i].key_orig, delay, dc_items[i].lastlogsize);
		}

		trx_config_clean(&cfg);

		DCconfig_clean_items(dc_items, errcodes, itemids.values_num);

		trx_free(errcodes);
		trx_free(dc_items);
	}

	trx_vector_uint64_destroy(&itemids);

	trx_strcpy_alloc(&buffer, &buffer_alloc, &buffer_offset, "TRX_EOF\n");

	treegix_log(LOG_LEVEL_DEBUG, "%s() sending [%s]", __func__, buffer);

	trx_alarm_on(CONFIG_TIMEOUT);
	if (SUCCEED != trx_tcp_send_raw(sock, buffer))
		trx_strlcpy(error, trx_socket_strerror(), MAX_STRING_LEN);
	else
		ret = SUCCEED;
	trx_alarm_off();

	trx_free(buffer);
out:
	if (FAIL == ret)
		treegix_log(LOG_LEVEL_WARNING, "cannot send list of active checks to \"%s\": %s", sock->peer, error);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_vector_str_append_uniq                                       *
 *                                                                            *
 * Purpose: append non duplicate string to the string vector                  *
 *                                                                            *
 * Parameters: vector - [IN/OUT] the string vector                            *
 *             str    - [IN] the string to append                             *
 *                                                                            *
 ******************************************************************************/
static void	trx_vector_str_append_uniq(trx_vector_str_t *vector, const char *str)
{
	if (FAIL == trx_vector_str_search(vector, str, TRX_DEFAULT_STR_COMPARE_FUNC))
		trx_vector_str_append(vector, trx_strdup(NULL, str));
}

/******************************************************************************
 *                                                                            *
 * Function: trx_itemkey_extract_global_regexps                               *
 *                                                                            *
 * Purpose: extract global regular expression names from item key             *
 *                                                                            *
 * Parameters: key     - [IN] the item key to parse                           *
 *             regexps - [OUT] the extracted regular expression names         *
 *                                                                            *
 ******************************************************************************/
static void	trx_itemkey_extract_global_regexps(const char *key, trx_vector_str_t *regexps)
{
#define TRX_KEY_LOG		1
#define TRX_KEY_EVENTLOG	2

	AGENT_REQUEST	request;
	int		item_key;
	const char	*param;

	if (0 == strncmp(key, "log[", 4) || 0 == strncmp(key, "logrt[", 6) || 0 == strncmp(key, "log.count[", 10) ||
			0 == strncmp(key, "logrt.count[", 12))
		item_key = TRX_KEY_LOG;
	else if (0 == strncmp(key, "eventlog[", 9))
		item_key = TRX_KEY_EVENTLOG;
	else
		return;

	init_request(&request);

	if(SUCCEED != parse_item_key(key, &request))
		goto out;

	/* "params" parameter */
	if (NULL != (param = get_rparam(&request, 1)) && '@' == *param)
		trx_vector_str_append_uniq(regexps, param + 1);

	if (TRX_KEY_EVENTLOG == item_key)
	{
		/* "severity" parameter */
		if (NULL != (param = get_rparam(&request, 2)) && '@' == *param)
			trx_vector_str_append_uniq(regexps, param + 1);

		/* "source" parameter */
		if (NULL != (param = get_rparam(&request, 3)) && '@' == *param)
			trx_vector_str_append_uniq(regexps, param + 1);

		/* "logeventid" parameter */
		if (NULL != (param = get_rparam(&request, 4)) && '@' == *param)
			trx_vector_str_append_uniq(regexps, param + 1);
	}
out:
	free_request(&request);
}

/******************************************************************************
 *                                                                            *
 * Function: send_list_of_active_checks_json                                  *
 *                                                                            *
 * Purpose: send list of active checks to the host                            *
 *                                                                            *
 * Parameters: sock - open socket of server-agent connection                  *
 *             json - request buffer                                          *
 *                                                                            *
 * Return value:  SUCCEED - list of active checks sent successfully           *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
int	send_list_of_active_checks_json(trx_socket_t *sock, struct trx_json_parse *jp)
{
	char			host[HOST_HOST_LEN_MAX], tmp[MAX_STRING_LEN], ip[INTERFACE_IP_LEN_MAX],
				error[MAX_STRING_LEN], *host_metadata = NULL, *interface = NULL;
	struct trx_json		json;
	int			ret = FAIL, i, version;
	trx_uint64_t		hostid;
	size_t			host_metadata_alloc = 1;	/* for at least NUL-termination char */
	size_t			interface_alloc = 1;		/* for at least NUL-termination char */
	unsigned short		port;
	trx_vector_uint64_t	itemids;
	trx_conn_flags_t	flag = TRX_CONN_DEFAULT;
	trx_config_t		cfg;

	trx_vector_ptr_t	regexps;
	trx_vector_str_t	names;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&regexps);
	trx_vector_str_create(&names);

	if (FAIL == trx_json_value_by_name(jp, TRX_PROTO_TAG_HOST, host, sizeof(host)))
	{
		trx_snprintf(error, MAX_STRING_LEN, "%s", trx_json_strerror());
		goto error;
	}

	host_metadata = (char *)trx_malloc(host_metadata, host_metadata_alloc);

	if (FAIL == trx_json_value_by_name_dyn(jp, TRX_PROTO_TAG_HOST_METADATA,
			&host_metadata, &host_metadata_alloc))
	{
		*host_metadata = '\0';
	}

	interface = (char *)trx_malloc(interface, interface_alloc);

	if (FAIL == trx_json_value_by_name_dyn(jp, TRX_PROTO_TAG_INTERFACE, &interface, &interface_alloc))
	{
		*interface = '\0';
	}
	else if (SUCCEED == is_ip(interface))
	{
		flag = TRX_CONN_IP;
	}
	else if (SUCCEED == trx_validate_hostname(interface))
	{
		flag = TRX_CONN_DNS;
	}
	else
	{
		trx_snprintf(error, MAX_STRING_LEN, "\"%s\" is not a valid IP or DNS", interface);
		goto error;
	}

	if (FAIL == trx_json_value_by_name(jp, TRX_PROTO_TAG_IP, ip, sizeof(ip)))
		strscpy(ip, sock->peer);

	if (FAIL == is_ip(ip))	/* check even if 'ip' came from get_ip_by_socket() - it can return not a valid IP */
	{
		trx_snprintf(error, MAX_STRING_LEN, "\"%s\" is not a valid IP address", ip);
		goto error;
	}

	if (FAIL == trx_json_value_by_name(jp, TRX_PROTO_TAG_PORT, tmp, sizeof(tmp)))
	{
		port = TRX_DEFAULT_AGENT_PORT;
	}
	else if (FAIL == is_ushort(tmp, &port))
	{
		trx_snprintf(error, MAX_STRING_LEN, "\"%s\" is not a valid port", tmp);
		goto error;
	}

	if (FAIL == get_hostid_by_host(sock, host, ip, port, host_metadata, flag, interface, &hostid, error))
		goto error;

	if (SUCCEED != trx_json_value_by_name(jp, TRX_PROTO_TAG_VERSION, tmp, sizeof(tmp)) ||
			FAIL == (version = trx_get_component_version(tmp)))
	{
		version = TRX_COMPONENT_VERSION(4, 2);
	}

	trx_vector_uint64_create(&itemids);
	trx_config_get(&cfg, TRX_CONFIG_FLAGS_REFRESH_UNSUPPORTED);

	get_list_of_active_checks(hostid, &itemids);

	trx_json_init(&json, TRX_JSON_STAT_BUF_LEN);
	trx_json_addstring(&json, TRX_PROTO_TAG_RESPONSE, TRX_PROTO_VALUE_SUCCESS, TRX_JSON_TYPE_STRING);
	trx_json_addarray(&json, TRX_PROTO_TAG_DATA);

	if (0 != itemids.values_num)
	{
		DC_ITEM		*dc_items;
		int		*errcodes, now, delay;

		dc_items = (DC_ITEM *)trx_malloc(NULL, sizeof(DC_ITEM) * itemids.values_num);
		errcodes = (int *)trx_malloc(NULL, sizeof(int) * itemids.values_num);

		DCconfig_get_items_by_itemids(dc_items, itemids.values, errcodes, itemids.values_num);

		now = time(NULL);

		for (i = 0; i < itemids.values_num; i++)
		{
			if (SUCCEED != errcodes[i])
			{
				treegix_log(LOG_LEVEL_DEBUG, "%s() Item [" TRX_FS_UI64 "] was not found in the"
						" server cache. Not sending now.", __func__, itemids.values[i]);
				continue;
			}

			if (ITEM_STATUS_ACTIVE != dc_items[i].status)
				continue;

			if (HOST_STATUS_MONITORED != dc_items[i].host.status)
				continue;

			if (TRX_COMPONENT_VERSION(4,4) > version && ITEM_STATE_NOTSUPPORTED == dc_items[i].state)
			{
				if (0 == cfg.refresh_unsupported)
					continue;

				if (dc_items[i].lastclock + cfg.refresh_unsupported > now)
					continue;
			}

			if (SUCCEED != trx_interval_preproc(dc_items[i].delay, &delay, NULL, NULL))
				continue;


			dc_items[i].key = trx_strdup(dc_items[i].key, dc_items[i].key_orig);
			substitute_key_macros(&dc_items[i].key, NULL, &dc_items[i], NULL, NULL, MACRO_TYPE_ITEM_KEY, NULL, 0);

			trx_json_addobject(&json, NULL);
			trx_json_addstring(&json, TRX_PROTO_TAG_KEY, dc_items[i].key, TRX_JSON_TYPE_STRING);

			if (TRX_COMPONENT_VERSION(4,4) > version)
			{
				if (0 != strcmp(dc_items[i].key, dc_items[i].key_orig))
				{
					trx_json_addstring(&json, TRX_PROTO_TAG_KEY_ORIG,
							dc_items[i].key_orig, TRX_JSON_TYPE_STRING);
				}

				trx_json_adduint64(&json, TRX_PROTO_TAG_DELAY, delay);
			}
			else
			{
				trx_json_adduint64(&json, TRX_PROTO_TAG_ITEMID, dc_items[i].itemid);
				trx_json_addstring(&json, TRX_PROTO_TAG_DELAY, dc_items[i].delay, TRX_JSON_TYPE_STRING);
			}

			/* The agent expects ALWAYS to have lastlogsize and mtime tags. */
			/* Removing those would cause older agents to fail. */
			trx_json_adduint64(&json, TRX_PROTO_TAG_LASTLOGSIZE, dc_items[i].lastlogsize);
			trx_json_adduint64(&json, TRX_PROTO_TAG_MTIME, dc_items[i].mtime);
			trx_json_close(&json);

			trx_itemkey_extract_global_regexps(dc_items[i].key, &names);

			trx_free(dc_items[i].key);
		}

		DCconfig_clean_items(dc_items, errcodes, itemids.values_num);

		trx_free(errcodes);
		trx_free(dc_items);
	}

	trx_json_close(&json);

	if (TRX_COMPONENT_VERSION(4,4) <= version)
		trx_json_adduint64(&json, TRX_PROTO_TAG_REFRESH_UNSUPPORTED, cfg.refresh_unsupported);

	trx_config_clean(&cfg);
	trx_vector_uint64_destroy(&itemids);

	DCget_expressions_by_names(&regexps, (const char * const *)names.values, names.values_num);

	if (0 < regexps.values_num)
	{
		char	buffer[32];

		trx_json_addarray(&json, TRX_PROTO_TAG_REGEXP);

		for (i = 0; i < regexps.values_num; i++)
		{
			trx_expression_t	*regexp = (trx_expression_t *)regexps.values[i];

			trx_json_addobject(&json, NULL);
			trx_json_addstring(&json, "name", regexp->name, TRX_JSON_TYPE_STRING);
			trx_json_addstring(&json, "expression", regexp->expression, TRX_JSON_TYPE_STRING);

			trx_snprintf(buffer, sizeof(buffer), "%d", regexp->expression_type);
			trx_json_addstring(&json, "expression_type", buffer, TRX_JSON_TYPE_INT);

			trx_snprintf(buffer, sizeof(buffer), "%c", regexp->exp_delimiter);
			trx_json_addstring(&json, "exp_delimiter", buffer, TRX_JSON_TYPE_STRING);

			trx_snprintf(buffer, sizeof(buffer), "%d", regexp->case_sensitive);
			trx_json_addstring(&json, "case_sensitive", buffer, TRX_JSON_TYPE_INT);

			trx_json_close(&json);
		}

		trx_json_close(&json);
	}

	treegix_log(LOG_LEVEL_DEBUG, "%s() sending [%s]", __func__, json.buffer);

	trx_alarm_on(CONFIG_TIMEOUT);
	if (SUCCEED != trx_tcp_send(sock, json.buffer))
		strscpy(error, trx_socket_strerror());
	else
		ret = SUCCEED;
	trx_alarm_off();

	trx_json_free(&json);

	goto out;
error:
	treegix_log(LOG_LEVEL_WARNING, "cannot send list of active checks to \"%s\": %s", sock->peer, error);

	trx_json_init(&json, TRX_JSON_STAT_BUF_LEN);
	trx_json_addstring(&json, TRX_PROTO_TAG_RESPONSE, TRX_PROTO_VALUE_FAILED, TRX_JSON_TYPE_STRING);
	trx_json_addstring(&json, TRX_PROTO_TAG_INFO, error, TRX_JSON_TYPE_STRING);

	treegix_log(LOG_LEVEL_DEBUG, "%s() sending [%s]", __func__, json.buffer);

	ret = trx_tcp_send(sock, json.buffer);

	trx_json_free(&json);
out:
	for (i = 0; i < names.values_num; i++)
		trx_free(names.values[i]);

	trx_vector_str_destroy(&names);

	trx_regexp_clean_expressions(&regexps);
	trx_vector_ptr_destroy(&regexps);

	trx_free(host_metadata);
	trx_free(interface);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}
