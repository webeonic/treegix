

#include "common.h"
#include "db.h"
#include "log.h"
#include "sysinfo.h"
#include "trxserver.h"
#include "trxtasks.h"

#include "proxy.h"
#include "dbcache.h"
#include "discovery.h"
#include "trxalgo.h"
#include "preproc.h"
#include "../trxcrypto/tls_tcp_active.h"
#include "trxlld.h"

extern char	*CONFIG_SERVER;

/* the space reserved in json buffer to hold at least one record plus service data */
#define TRX_DATA_JSON_RESERVED		(HISTORY_TEXT_VALUE_LEN * 4 + TRX_KIBIBYTE * 4)

#define TRX_DATA_JSON_RECORD_LIMIT	(TRX_MAX_RECV_DATA_SIZE - TRX_DATA_JSON_RESERVED)
#define TRX_DATA_JSON_BATCH_LIMIT	((TRX_MAX_RECV_DATA_SIZE - TRX_DATA_JSON_RESERVED) / 2)

/* the maximum number of values processed in one batch */
#define TRX_HISTORY_VALUES_MAX		256

typedef struct
{
	trx_uint64_t		druleid;
	trx_vector_ptr_t	ips;
}
trx_drule_t;

typedef struct
{
	char			ip[INTERFACE_IP_LEN_MAX];
	trx_vector_ptr_t	services;
}
trx_drule_ip_t;

extern unsigned int	configured_tls_accept_modes;

typedef struct
{
	const char		*field;
	const char		*tag;
	trx_json_type_t		jt;
	const char		*default_value;
}
trx_history_field_t;

typedef struct
{
	const char		*table, *lastidfield;
	trx_history_field_t	fields[TRX_MAX_FIELDS];
}
trx_history_table_t;

typedef struct
{
	trx_uint64_t	id;
	size_t		offset;
}
trx_id_offset_t;


typedef int	(*trx_client_item_validator_t)(DC_ITEM *item, trx_socket_t *sock, void *args, char **error);

typedef struct
{
	trx_uint64_t	hostid;
	int		value;
}
trx_host_rights_t;

static trx_history_table_t	dht = {
	"proxy_dhistory", "dhistory_lastid",
		{
		{"clock",		TRX_PROTO_TAG_CLOCK,		TRX_JSON_TYPE_INT,	NULL},
		{"druleid",		TRX_PROTO_TAG_DRULE,		TRX_JSON_TYPE_INT,	NULL},
		{"dcheckid",		TRX_PROTO_TAG_DCHECK,		TRX_JSON_TYPE_INT,	NULL},
		{"ip",			TRX_PROTO_TAG_IP,		TRX_JSON_TYPE_STRING,	NULL},
		{"dns",			TRX_PROTO_TAG_DNS,		TRX_JSON_TYPE_STRING,	NULL},
		{"port",		TRX_PROTO_TAG_PORT,		TRX_JSON_TYPE_INT,	"0"},
		{"value",		TRX_PROTO_TAG_VALUE,		TRX_JSON_TYPE_STRING,	""},
		{"status",		TRX_PROTO_TAG_STATUS,		TRX_JSON_TYPE_INT,	"0"},
		{NULL}
		}
};

static trx_history_table_t	areg = {
	"proxy_autoreg_host", "autoreg_host_lastid",
		{
		{"clock",		TRX_PROTO_TAG_CLOCK,		TRX_JSON_TYPE_INT,	NULL},
		{"host",		TRX_PROTO_TAG_HOST,		TRX_JSON_TYPE_STRING,	NULL},
		{"listen_ip",		TRX_PROTO_TAG_IP,		TRX_JSON_TYPE_STRING,	""},
		{"listen_dns",		TRX_PROTO_TAG_DNS,		TRX_JSON_TYPE_STRING,	""},
		{"listen_port",		TRX_PROTO_TAG_PORT,		TRX_JSON_TYPE_STRING,	"0"},
		{"host_metadata",	TRX_PROTO_TAG_HOST_METADATA,	TRX_JSON_TYPE_STRING,	""},
		{"flags",		TRX_PROTO_TAG_FLAGS,		TRX_JSON_TYPE_STRING,	"0"},
		{"tls_accepted",	TRX_PROTO_TAG_TLS_ACCEPTED,	TRX_JSON_TYPE_INT,	"0"},
		{NULL}
		}
};

static const char	*availability_tag_available[TRX_AGENT_MAX] = {TRX_PROTO_TAG_AVAILABLE,
					TRX_PROTO_TAG_SNMP_AVAILABLE, TRX_PROTO_TAG_IPMI_AVAILABLE,
					TRX_PROTO_TAG_JMX_AVAILABLE};
static const char	*availability_tag_error[TRX_AGENT_MAX] = {TRX_PROTO_TAG_ERROR,
					TRX_PROTO_TAG_SNMP_ERROR, TRX_PROTO_TAG_IPMI_ERROR,
					TRX_PROTO_TAG_JMX_ERROR};

/******************************************************************************
 *                                                                            *
 * Function: trx_proxy_check_permissions                                      *
 *                                                                            *
 * Purpose: check proxy connection permissions (encryption configuration and  *
 *          if peer proxy address is allowed)                                 *
 *                                                                            *
 * Parameters:                                                                *
 *     proxy   - [IN] the proxy data                                          *
 *     sock    - [IN] connection socket context                               *
 *     error   - [OUT] error message                                          *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - connection permission check was successful                   *
 *     FAIL    - otherwise                                                    *
 *                                                                            *
 ******************************************************************************/
int	trx_proxy_check_permissions(const DC_PROXY *proxy, const trx_socket_t *sock, char **error)
{
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	trx_tls_conn_attr_t	attr;
#endif
	if ('\0' != *proxy->proxy_address && FAIL == trx_tcp_check_allowed_peers(sock, proxy->proxy_address))
	{
		*error = trx_strdup(*error, "connection is not allowed");
		return FAIL;
	}

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (TRX_TCP_SEC_TLS_CERT == sock->connection_type)
	{
		if (SUCCEED != trx_tls_get_attr_cert(sock, &attr))
		{
			*error = trx_strdup(*error, "internal error: cannot get connection attributes");
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
		}
	}
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || (defined(HAVE_OPENSSL) && defined(HAVE_OPENSSL_WITH_PSK))
	else if (TRX_TCP_SEC_TLS_PSK == sock->connection_type)
	{
		if (SUCCEED != trx_tls_get_attr_psk(sock, &attr))
		{
			*error = trx_strdup(*error, "internal error: cannot get connection attributes");
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
		}
	}
#endif
	else if (TRX_TCP_SEC_UNENCRYPTED != sock->connection_type)
	{
		*error = trx_strdup(*error, "internal error: invalid connection type");
		THIS_SHOULD_NEVER_HAPPEN;
		return FAIL;
	}
#endif
	if (0 == ((unsigned int)proxy->tls_accept & sock->connection_type))
	{
		*error = trx_dsprintf(NULL, "connection of type \"%s\" is not allowed for proxy \"%s\"",
				trx_tcp_connection_type_name(sock->connection_type), proxy->host);
		return FAIL;
	}

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (TRX_TCP_SEC_TLS_CERT == sock->connection_type)
	{
		/* simplified match, not compliant with RFC 4517, 4518 */
		if ('\0' != *proxy->tls_issuer && 0 != strcmp(proxy->tls_issuer, attr.issuer))
		{
			*error = trx_dsprintf(*error, "proxy \"%s\" certificate issuer does not match", proxy->host);
			return FAIL;
		}

		/* simplified match, not compliant with RFC 4517, 4518 */
		if ('\0' != *proxy->tls_subject && 0 != strcmp(proxy->tls_subject, attr.subject))
		{
			*error = trx_dsprintf(*error, "proxy \"%s\" certificate subject does not match", proxy->host);
			return FAIL;
		}
	}
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || (defined(HAVE_OPENSSL) && defined(HAVE_OPENSSL_WITH_PSK))
	else if (TRX_TCP_SEC_TLS_PSK == sock->connection_type)
	{
		if (strlen(proxy->tls_psk_identity) != attr.psk_identity_len ||
				0 != memcmp(proxy->tls_psk_identity, attr.psk_identity, attr.psk_identity_len))
		{
			*error = trx_dsprintf(*error, "proxy \"%s\" is using false PSK identity", proxy->host);
			return FAIL;
		}
	}
#endif
#endif
	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_host_check_permissions                                       *
 *                                                                            *
 * Purpose: checks host connection permissions (encryption configuration)     *
 *                                                                            *
 * Parameters:                                                                *
 *     host  - [IN] the host data                                             *
 *     sock  - [IN] connection socket context                                 *
 *     error - [OUT] error message                                            *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - connection permission check was successful                   *
 *     FAIL    - otherwise                                                    *
 *                                                                            *
 ******************************************************************************/
static int	trx_host_check_permissions(const DC_HOST *host, const trx_socket_t *sock, char **error)
{
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	trx_tls_conn_attr_t	attr;

	if (TRX_TCP_SEC_TLS_CERT == sock->connection_type)
	{
		if (SUCCEED != trx_tls_get_attr_cert(sock, &attr))
		{
			*error = trx_strdup(*error, "internal error: cannot get connection attributes");
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
		}
	}
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || (defined(HAVE_OPENSSL) && defined(HAVE_OPENSSL_WITH_PSK))
	else if (TRX_TCP_SEC_TLS_PSK == sock->connection_type)
	{
		if (SUCCEED != trx_tls_get_attr_psk(sock, &attr))
		{
			*error = trx_strdup(*error, "internal error: cannot get connection attributes");
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
		}
	}
#endif
	else if (TRX_TCP_SEC_UNENCRYPTED != sock->connection_type)
	{
		*error = trx_strdup(*error, "internal error: invalid connection type");
		THIS_SHOULD_NEVER_HAPPEN;
		return FAIL;
	}
#endif
	if (0 == ((unsigned int)host->tls_accept & sock->connection_type))
	{
		*error = trx_dsprintf(NULL, "connection of type \"%s\" is not allowed for host \"%s\"",
				trx_tcp_connection_type_name(sock->connection_type), host->host);
		return FAIL;
	}

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (TRX_TCP_SEC_TLS_CERT == sock->connection_type)
	{
		/* simplified match, not compliant with RFC 4517, 4518 */
		if ('\0' != *host->tls_issuer && 0 != strcmp(host->tls_issuer, attr.issuer))
		{
			*error = trx_dsprintf(*error, "host \"%s\" certificate issuer does not match", host->host);
			return FAIL;
		}

		/* simplified match, not compliant with RFC 4517, 4518 */
		if ('\0' != *host->tls_subject && 0 != strcmp(host->tls_subject, attr.subject))
		{
			*error = trx_dsprintf(*error, "host \"%s\" certificate subject does not match", host->host);
			return FAIL;
		}
	}
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || (defined(HAVE_OPENSSL) && defined(HAVE_OPENSSL_WITH_PSK))
	else if (TRX_TCP_SEC_TLS_PSK == sock->connection_type)
	{
		if (strlen(host->tls_psk_identity) != attr.psk_identity_len ||
				0 != memcmp(host->tls_psk_identity, attr.psk_identity, attr.psk_identity_len))
		{
			*error = trx_dsprintf(*error, "host \"%s\" is using false PSK identity", host->host);
			return FAIL;
		}
	}
#endif
#endif
	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: get_active_proxy_from_request                                    *
 *                                                                            *
 * Purpose:                                                                   *
 *     Extract a proxy name from JSON and find the proxy ID in configuration  *
 *     cache, and check access rights. The proxy must be configured in active *
 *     mode.                                                                  *
 *                                                                            *
 * Parameters:                                                                *
 *     jp      - [IN] JSON with the proxy name                                *
 *     proxy   - [OUT] the proxy data                                         *
 *     error   - [OUT] error message                                          *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - proxy ID was found in database                               *
 *     FAIL    - an error occurred (e.g. an unknown proxy, the proxy is       *
 *               configured in passive mode or access denied)                 *
 *                                                                            *
 ******************************************************************************/
int	get_active_proxy_from_request(struct trx_json_parse *jp, DC_PROXY *proxy, char **error)
{
	char	*ch_error, host[HOST_HOST_LEN_MAX];

	if (SUCCEED != trx_json_value_by_name(jp, TRX_PROTO_TAG_HOST, host, HOST_HOST_LEN_MAX))
	{
		*error = trx_strdup(*error, "missing name of proxy");
		return FAIL;
	}

	if (SUCCEED != trx_check_hostname(host, &ch_error))
	{
		*error = trx_dsprintf(*error, "invalid proxy name \"%s\": %s", host, ch_error);
		trx_free(ch_error);
		return FAIL;
	}

	return trx_dc_get_active_proxy_by_name(host, proxy, error);
}

/******************************************************************************
 *                                                                            *
 * Function: check_access_passive_proxy                                       *
 *                                                                            *
 * Purpose:                                                                   *
 *     Check access rights to a passive proxy for the given connection and    *
 *     send a response if denied.                                             *
 *                                                                            *
 * Parameters:                                                                *
 *     sock          - [IN] connection socket context                         *
 *     send_response - [IN] to send or not to send a response to server.      *
 *                          Value: TRX_SEND_RESPONSE or                       *
 *                          TRX_DO_NOT_SEND_RESPONSE                          *
 *     req           - [IN] request, included into error message              *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - access is allowed                                            *
 *     FAIL    - access is denied                                             *
 *                                                                            *
 ******************************************************************************/
int	check_access_passive_proxy(trx_socket_t *sock, int send_response, const char *req)
{
	char	*msg = NULL;

	if (FAIL == trx_tcp_check_allowed_peers(sock, CONFIG_SERVER))
	{
		treegix_log(LOG_LEVEL_WARNING, "%s from server \"%s\" is not allowed: %s", req, sock->peer,
				trx_socket_strerror());

		if (TRX_SEND_RESPONSE == send_response)
			trx_send_proxy_response(sock, FAIL, "connection is not allowed", CONFIG_TIMEOUT);

		return FAIL;
	}

	if (0 == (configured_tls_accept_modes & sock->connection_type))
	{
		msg = trx_dsprintf(NULL, "%s over connection of type \"%s\" is not allowed", req,
				trx_tcp_connection_type_name(sock->connection_type));

		treegix_log(LOG_LEVEL_WARNING, "%s from server \"%s\" by proxy configuration parameter \"TLSAccept\"",
				msg, sock->peer);

		if (TRX_SEND_RESPONSE == send_response)
			trx_send_proxy_response(sock, FAIL, msg, CONFIG_TIMEOUT);

		trx_free(msg);
		return FAIL;
	}

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (TRX_TCP_SEC_TLS_CERT == sock->connection_type)
	{
		if (SUCCEED == trx_check_server_issuer_subject(sock, &msg))
			return SUCCEED;

		treegix_log(LOG_LEVEL_WARNING, "%s from server \"%s\" is not allowed: %s", req, sock->peer, msg);

		if (TRX_SEND_RESPONSE == send_response)
			trx_send_proxy_response(sock, FAIL, "certificate issuer or subject mismatch", CONFIG_TIMEOUT);

		trx_free(msg);
		return FAIL;
	}
	else if (TRX_TCP_SEC_TLS_PSK == sock->connection_type)
	{
		if (0 != (TRX_PSK_FOR_PROXY & trx_tls_get_psk_usage()))
			return SUCCEED;

		treegix_log(LOG_LEVEL_WARNING, "%s from server \"%s\" is not allowed: it used PSK which is not"
				" configured for proxy communication with server", req, sock->peer);

		if (TRX_SEND_RESPONSE == send_response)
			trx_send_proxy_response(sock, FAIL, "wrong PSK used", CONFIG_TIMEOUT);

		return FAIL;
	}
#endif
	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: proxyconfig_add_row                                              *
 *                                                                            *
 * Purpose: add database row to the proxy config json data                    *
 *                                                                            *
 * Parameters: j     - [OUT] the output json                                  *
 *             row   - [IN] the database row to add                           *
 *             table - [IN] the table configuration                           *
 *                                                                            *
 ******************************************************************************/
static void	proxyconfig_add_row(struct trx_json *j, const DB_ROW row, const TRX_TABLE *table)
{
	int	fld = 0, i;

	trx_json_addstring(j, NULL, row[fld++], TRX_JSON_TYPE_INT);

	for (i = 0; 0 != table->fields[i].name; i++)
	{
		if (0 == (table->fields[i].flags & TRX_PROXY))
			continue;

		switch (table->fields[i].type)
		{
			case TRX_TYPE_INT:
			case TRX_TYPE_UINT:
			case TRX_TYPE_ID:
				if (SUCCEED != DBis_null(row[fld]))
					trx_json_addstring(j, NULL, row[fld], TRX_JSON_TYPE_INT);
				else
					trx_json_addstring(j, NULL, NULL, TRX_JSON_TYPE_NULL);
				break;
			default:
				trx_json_addstring(j, NULL, row[fld], TRX_JSON_TYPE_STRING);
				break;
		}
		fld++;
	}
}

typedef struct
{
	trx_uint64_t	itemid;
	trx_uint64_t	master_itemid;
	struct trx_json	data;
}
trx_proxy_item_config_t;

/******************************************************************************
 *                                                                            *
 * Function: get_proxyconfig_table_items                                      *
 *                                                                            *
 * Purpose: prepare items table proxy configuration data                      *
 *                                                                            *
 ******************************************************************************/
static int	get_proxyconfig_table_items(trx_uint64_t proxy_hostid, struct trx_json *j, const TRX_TABLE *table,
		trx_vector_uint64_t *itemids)
{
	char			*sql = NULL;
	size_t			sql_alloc = 4 * TRX_KIBIBYTE, sql_offset = 0;
	int			f, fld, fld_type = -1, fld_key = -1, fld_master = -1, ret = SUCCEED;
	DB_RESULT		result;
	DB_ROW			row;
	trx_hashset_t		proxy_items, itemids_added;
	struct trx_json		*jrow;
	trx_vector_ptr_t	items;
	trx_uint64_t		itemid, *pitemid;
	trx_hashset_iter_t	iter;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() proxy_hostid:" TRX_FS_UI64, __func__, proxy_hostid);

	trx_hashset_create(&itemids_added, 100, TRX_DEFAULT_UINT64_HASH_FUNC, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	trx_json_addobject(j, table->table);
	trx_json_addarray(j, "fields");

	sql = (char *)trx_malloc(sql, sql_alloc);

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "select t.%s", table->recid);

	trx_json_addstring(j, NULL, table->recid, TRX_JSON_TYPE_STRING);

	for (f = 0, fld = 1; 0 != table->fields[f].name; f++)
	{
		if (0 == (table->fields[f].flags & TRX_PROXY))
			continue;

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ",t.");
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, table->fields[f].name);

		trx_json_addstring(j, NULL, table->fields[f].name, TRX_JSON_TYPE_STRING);

		if (0 == strcmp(table->fields[f].name, "type"))
			fld_type = fld;
		else if (0 == strcmp(table->fields[f].name, "key_"))
			fld_key = fld;
		else if (0 == strcmp(table->fields[f].name, "master_itemid"))
			fld_master = fld;
		fld++;
	}

	if (-1 == fld_type || -1 == fld_key || -1 == fld_master)
	{
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	trx_json_close(j);	/* fields */

	trx_json_addarray(j, "data");

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			" from items t,hosts r where t.hostid=r.hostid"
				" and r.proxy_hostid=" TRX_FS_UI64
				" and r.status in (%d,%d)"
				" and t.flags<>%d"
				" and t.type in (%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d)"
			" order by t.%s",
			proxy_hostid,
			HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED,
			TRX_FLAG_DISCOVERY_PROTOTYPE,
			ITEM_TYPE_TREEGIX, ITEM_TYPE_TREEGIX_ACTIVE, ITEM_TYPE_SNMPv1, ITEM_TYPE_SNMPv2c,
			ITEM_TYPE_SNMPv3, ITEM_TYPE_IPMI, ITEM_TYPE_TRAPPER, ITEM_TYPE_SIMPLE,
			ITEM_TYPE_HTTPTEST, ITEM_TYPE_EXTERNAL, ITEM_TYPE_DB_MONITOR, ITEM_TYPE_SSH,
			ITEM_TYPE_TELNET, ITEM_TYPE_JMX, ITEM_TYPE_SNMPTRAP, ITEM_TYPE_INTERNAL,
			ITEM_TYPE_HTTPAGENT, ITEM_TYPE_DEPENDENT,
			table->recid);

	if (NULL == (result = DBselect("%s", sql)))
	{
		ret = FAIL;
		goto skip_data;
	}

	trx_hashset_create(&proxy_items, 1000, TRX_DEFAULT_UINT64_HASH_FUNC, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	while (NULL != (row = DBfetch(result)))
	{
		if (SUCCEED == is_item_processed_by_server(atoi(row[fld_type]), row[fld_key]))
			continue;

		if (SUCCEED != DBis_null(row[fld_master]))
		{
			trx_proxy_item_config_t	proxy_item_local, *proxy_item;

			TRX_STR2UINT64(proxy_item_local.itemid, row[0]);
			TRX_STR2UINT64(proxy_item_local.master_itemid, row[fld_master]);
			proxy_item = trx_hashset_insert(&proxy_items, &proxy_item_local, sizeof(proxy_item_local));
			trx_json_initarray(&proxy_item->data, 256);
			jrow = &proxy_item->data;
		}
		else
		{
			TRX_STR2UINT64(itemid, row[0]);
			trx_hashset_insert(&itemids_added, &itemid, sizeof(itemid));
			trx_json_addarray(j, NULL);
			jrow = j;
		}

		proxyconfig_add_row(jrow, row, table);
		trx_json_close(jrow);
	}

	/* flush cached dependent items */

	trx_vector_ptr_create(&items);
	while (0 != proxy_items.num_data)
	{
		trx_proxy_item_config_t	*proxy_item;
		int			i;

		trx_hashset_iter_reset(&proxy_items, &iter);
		while (NULL != (proxy_item = (trx_proxy_item_config_t *)trx_hashset_iter_next(&iter)))
		{
			if (NULL == trx_hashset_search(&proxy_items, &proxy_item->master_itemid))
				trx_vector_ptr_append(&items, proxy_item);
		}

		if (0 == items.values_num)
		{
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
		}

		trx_vector_ptr_sort(&items, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
		for (i = 0; i < items.values_num; i++)
		{
			proxy_item = (trx_proxy_item_config_t *)items.values[i];
			if (NULL != trx_hashset_search(&itemids_added, &proxy_item->master_itemid))
			{
				trx_hashset_insert(&itemids_added, &proxy_item->itemid, sizeof(itemid));
				trx_json_addraw(j, NULL, proxy_item->data.buffer);
			}
			trx_json_free(&proxy_item->data);
			trx_hashset_remove_direct(&proxy_items, proxy_item);
		}

		trx_vector_ptr_clear(&items);
	}
	trx_vector_ptr_destroy(&items);
	trx_hashset_destroy(&proxy_items);

	DBfree_result(result);

	trx_hashset_iter_reset(&itemids_added, &iter);
	while (NULL != (pitemid = (trx_uint64_t *)trx_hashset_iter_next(&iter)))
		trx_vector_uint64_append(itemids, *pitemid);
	trx_vector_uint64_sort(itemids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
skip_data:
	trx_free(sql);

	trx_json_close(j);	/* data */
	trx_json_close(j);	/* table->table */
	trx_hashset_destroy(&itemids_added);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

#define TRX_MAX_IDS_PER_SELECT 10000

/******************************************************************************
 *                                                                            *
 * Function: get_proxyconfig_table_items                                      *
 *                                                                            *
 * Purpose: prepare items table proxy configuration data                      *
 *                                                                            *
 ******************************************************************************/
static int	get_proxyconfig_table_items_ext(const trx_vector_uint64_t *itemids, struct trx_json *j,
		const TRX_TABLE *table)
{
	char		*sql = NULL;
	size_t		sql_alloc = 4 * TRX_KIBIBYTE, sql_offset = 0, filter_offset;
	int		f, ret = SUCCEED, i;
	DB_RESULT	result;
	DB_ROW		row;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() table:%s", __func__, table->table);

	trx_json_addobject(j, table->table);
	trx_json_addarray(j, "fields");

	sql = (char *)trx_malloc(sql, sql_alloc);

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "select t.%s", table->recid);

	trx_json_addstring(j, NULL, table->recid, TRX_JSON_TYPE_STRING);

	for (f = 0; 0 != table->fields[f].name; f++)
	{
		if (0 == (table->fields[f].flags & TRX_PROXY))
			continue;

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ",t.");
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, table->fields[f].name);
		trx_json_addstring(j, NULL, table->fields[f].name, TRX_JSON_TYPE_STRING);
	}

	trx_json_close(j);	/* fields */

	trx_json_addarray(j, "data");

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " from %s t where", table->table);
	filter_offset = sql_offset;
	for (i = 0; i < itemids->values_num; i += TRX_MAX_IDS_PER_SELECT)
	{
		int	values_num;
		values_num = MIN(i + TRX_MAX_IDS_PER_SELECT, itemids->values_num) - i;
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "t.itemid", itemids->values + i, values_num);

		if (NULL == (result = DBselect("%s", sql)))
		{
			ret = FAIL;
			goto skip_data;
		}

		while (NULL != (row = DBfetch(result)))
		{
			trx_json_addarray(j, NULL);
			proxyconfig_add_row(j, row, table);
			trx_json_close(j);
		}
		DBfree_result(result);
		sql_offset = filter_offset;
	}
skip_data:
	trx_free(sql);

	trx_json_close(j);	/* data */
	trx_json_close(j);	/* table->table */

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: get_proxyconfig_table                                            *
 *                                                                            *
 * Purpose: prepare proxy configuration data                                  *
 *                                                                            *
 ******************************************************************************/
static int	get_proxyconfig_table(trx_uint64_t proxy_hostid, struct trx_json *j, const TRX_TABLE *table,
		trx_vector_uint64_t *hosts, trx_vector_uint64_t *httptests)
{
	char		*sql = NULL;
	size_t		sql_alloc = 4 * TRX_KIBIBYTE, sql_offset = 0;
	int		f, ret = SUCCEED;
	DB_RESULT	result;
	DB_ROW		row;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() proxy_hostid:" TRX_FS_UI64 " table:'%s'",
			__func__, proxy_hostid, table->table);

	trx_json_addobject(j, table->table);
	trx_json_addarray(j, "fields");

	sql = (char *)trx_malloc(sql, sql_alloc);

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "select t.%s", table->recid);

	trx_json_addstring(j, NULL, table->recid, TRX_JSON_TYPE_STRING);

	for (f = 0; 0 != table->fields[f].name; f++)
	{
		if (0 == (table->fields[f].flags & TRX_PROXY))
			continue;

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ",t.");
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, table->fields[f].name);

		trx_json_addstring(j, NULL, table->fields[f].name, TRX_JSON_TYPE_STRING);
	}

	trx_json_close(j);	/* fields */

	trx_json_addarray(j, "data");

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " from %s t", table->table);

	if (SUCCEED == str_in_list("hosts,interface,hosts_templates,hostmacro", table->table, ','))
	{
		if (0 == hosts->values_num)
			goto skip_data;

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "t.hostid", hosts->values, hosts->values_num);
	}
	else if (0 == strcmp(table->table, "drules"))
	{
		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				" where t.proxy_hostid=" TRX_FS_UI64
					" and t.status=%d",
				proxy_hostid, DRULE_STATUS_MONITORED);
	}
	else if (0 == strcmp(table->table, "dchecks"))
	{
		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				",drules r where t.druleid=r.druleid"
					" and r.proxy_hostid=" TRX_FS_UI64
					" and r.status=%d",
				proxy_hostid, DRULE_STATUS_MONITORED);
	}
	else if (0 == strcmp(table->table, "hstgrp"))
	{
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ",config r where t.groupid=r.discovery_groupid");
	}
	else if (SUCCEED == str_in_list("httptest,httptest_field,httptestitem,httpstep", table->table, ','))
	{
		if (0 == httptests->values_num)
			goto skip_data;

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "t.httptestid",
				httptests->values, httptests->values_num);
	}
	else if (SUCCEED == str_in_list("httpstepitem,httpstep_field", table->table, ','))
	{
		if (0 == httptests->values_num)
			goto skip_data;

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				",httpstep r where t.httpstepid=r.httpstepid"
					" and");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "r.httptestid",
				httptests->values, httptests->values_num);
	}

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by t.");
	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, table->recid);

	if (NULL == (result = DBselect("%s", sql)))
	{
		ret = FAIL;
		goto skip_data;
	}

	while (NULL != (row = DBfetch(result)))
	{
		trx_json_addarray(j, NULL);
		proxyconfig_add_row(j, row, table);
		trx_json_close(j);
	}
	DBfree_result(result);
skip_data:
	trx_free(sql);

	trx_json_close(j);	/* data */
	trx_json_close(j);	/* table->table */

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

static void	get_proxy_monitored_hosts(trx_uint64_t proxy_hostid, trx_vector_uint64_t *hosts)
{
	DB_RESULT	result;
	DB_ROW		row;
	trx_uint64_t	hostid, *ids = NULL;
	int		ids_alloc = 0, ids_num = 0;
	char		*sql = NULL;
	size_t		sql_alloc = 512, sql_offset;

	sql = (char *)trx_malloc(sql, sql_alloc * sizeof(char));

	result = DBselect(
			"select hostid"
			" from hosts"
			" where proxy_hostid=" TRX_FS_UI64
				" and status in (%d,%d)"
				" and flags<>%d",
			proxy_hostid, HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED, TRX_FLAG_DISCOVERY_PROTOTYPE);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(hostid, row[0]);

		trx_vector_uint64_append(hosts, hostid);
		uint64_array_add(&ids, &ids_alloc, &ids_num, hostid, 64);
	}
	DBfree_result(result);

	while (0 != ids_num)
	{
		sql_offset = 0;
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select distinct templateid"
				" from hosts_templates"
				" where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid", ids, ids_num);

		ids_num = 0;

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			TRX_STR2UINT64(hostid, row[0]);

			trx_vector_uint64_append(hosts, hostid);
			uint64_array_add(&ids, &ids_alloc, &ids_num, hostid, 64);
		}
		DBfree_result(result);
	}

	trx_free(ids);
	trx_free(sql);

	trx_vector_uint64_sort(hosts, TRX_DEFAULT_UINT64_COMPARE_FUNC);
}

static void	get_proxy_monitored_httptests(trx_uint64_t proxy_hostid, trx_vector_uint64_t *httptests)
{
	DB_RESULT	result;
	DB_ROW		row;
	trx_uint64_t	httptestid;

	result = DBselect(
			"select httptestid"
			" from httptest t,hosts h"
			" where t.hostid=h.hostid"
				" and t.status=%d"
				" and h.proxy_hostid=" TRX_FS_UI64
				" and h.status=%d",
			HTTPTEST_STATUS_MONITORED, proxy_hostid, HOST_STATUS_MONITORED);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(httptestid, row[0]);

		trx_vector_uint64_append(httptests, httptestid);
	}
	DBfree_result(result);

	trx_vector_uint64_sort(httptests, TRX_DEFAULT_UINT64_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: get_proxyconfig_data                                             *
 *                                                                            *
 * Purpose: prepare proxy configuration data                                  *
 *                                                                            *
 ******************************************************************************/
int	get_proxyconfig_data(trx_uint64_t proxy_hostid, struct trx_json *j, char **error)
{
	static const char	*proxytable[] =
	{
		"globalmacro",
		"hosts",
		"interface",
		"hosts_templates",
		"hostmacro",
		"items",
		"item_rtdata",
		"item_preproc",
		"drules",
		"dchecks",
		"regexps",
		"expressions",
		"hstgrp",
		"config",
		"httptest",
		"httptestitem",
		"httptest_field",
		"httpstep",
		"httpstepitem",
		"httpstep_field",
		"config_autoreg_tls",
		NULL
	};

	int			i, ret = FAIL;
	const TRX_TABLE		*table;
	trx_vector_uint64_t	hosts, httptests;
	trx_vector_uint64_t	itemids;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() proxy_hostid:" TRX_FS_UI64, __func__, proxy_hostid);

	trx_vector_uint64_create(&itemids);
	trx_vector_uint64_create(&hosts);
	trx_vector_uint64_create(&httptests);

	DBbegin();
	get_proxy_monitored_hosts(proxy_hostid, &hosts);
	get_proxy_monitored_httptests(proxy_hostid, &httptests);

	for (i = 0; NULL != proxytable[i]; i++)
	{
		table = DBget_table(proxytable[i]);

		if (0 == strcmp(proxytable[i], "items"))
		{
			ret = get_proxyconfig_table_items(proxy_hostid, j, table, &itemids);
		}
		else if (0 == strcmp(proxytable[i], "item_preproc") || 0 == strcmp(proxytable[i], "item_rtdata"))
		{
			if (0 != itemids.values_num)
				ret = get_proxyconfig_table_items_ext(&itemids, j, table);
		}
		else
			ret = get_proxyconfig_table(proxy_hostid, j, table, &hosts, &httptests);

		if (SUCCEED != ret)
		{
			*error = trx_dsprintf(*error, "failed to get data from table \"%s\"", table->table);
			goto out;
		}
	}

	ret = SUCCEED;
out:
	DBcommit();
	trx_vector_uint64_destroy(&httptests);
	trx_vector_uint64_destroy(&hosts);
	trx_vector_uint64_destroy(&itemids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: remember_record                                                  *
 *                                                                            *
 * Purpose: A record is stored as a sequence of fields and flag bytes for     *
 *          handling NULL values. A field is stored as a null-terminated      *
 *          string to preserve field boundaries. If a field value can be NULL *
 *          a flag byte is inserted after the field to distinguish between    *
 *          empty string and NULL value. The flag byte can be '\1'            *
 *          (not NULL value) or '\2' (NULL value).                            *
 *                                                                            *
 * Examples of representation:                                                *
 *          \0\2    - the field can be NULL and it is NULL                    *
 *          \0\1    - the field can be NULL but is empty string               *
 *          abc\0\1 - the field can be NULL but is a string "abc"             *
 *          \0      - the field can not be NULL and is empty string           *
 *          abc\0   - the field can not be NULL and is a string "abc"         *
 *                                                                            *
 ******************************************************************************/
static void	remember_record(const TRX_FIELD **fields, int fields_count, char **recs, size_t *recs_alloc,
		size_t *recs_offset, DB_ROW row)
{
	int	f;

	for (f = 0; f < fields_count; f++)
	{
		if (0 != (fields[f]->flags & TRX_NOTNULL))
		{
			trx_strcpy_alloc(recs, recs_alloc, recs_offset, row[f]);
			*recs_offset += sizeof(char);
		}
		else if (SUCCEED != DBis_null(row[f]))
		{
			trx_strcpy_alloc(recs, recs_alloc, recs_offset, row[f]);
			*recs_offset += sizeof(char);
			trx_chrcpy_alloc(recs, recs_alloc, recs_offset, '\1');
		}
		else
		{
			trx_strcpy_alloc(recs, recs_alloc, recs_offset, "");
			*recs_offset += sizeof(char);
			trx_chrcpy_alloc(recs, recs_alloc, recs_offset, '\2');
		}
	}
}

static trx_hash_t	id_offset_hash_func(const void *data)
{
	const trx_id_offset_t *p = (trx_id_offset_t *)data;

	return TRX_DEFAULT_UINT64_HASH_ALGO(&p->id, sizeof(trx_uint64_t), TRX_DEFAULT_HASH_SEED);
}

static int	id_offset_compare_func(const void *d1, const void *d2)
{
	const trx_id_offset_t *p1 = (trx_id_offset_t *)d1, *p2 = (trx_id_offset_t *)d2;

	return TRX_DEFAULT_UINT64_COMPARE_FUNC(&p1->id, &p2->id);
}

/******************************************************************************
 *                                                                            *
 * Function: find_field_by_name                                               *
 *                                                                            *
 * Purpose: find a number of the field                                        *
 *                                                                            *
 ******************************************************************************/
static int	find_field_by_name(const TRX_FIELD **fields, int fields_count, const char *field_name)
{
	int	f;

	for (f = 0; f < fields_count; f++)
	{
		if (0 == strcmp(fields[f]->name, field_name))
			break;
	}

	return f;
}

/******************************************************************************
 *                                                                            *
 * Function: compare_nth_field                                                *
 *                                                                            *
 * Purpose: This function compares a value from JSON record with the value    *
 *          of the n-th field of DB record. For description how DB record is  *
 *          stored in memory see comments in function remember_record().      *
 *                                                                            *
 * Comparing deals with 4 cases:                                              *
 *          - JSON value is not NULL, DB value is not NULL                    *
 *          - JSON value is not NULL, DB value is NULL                        *
 *          - JSON value is NULL, DB value is NULL                            *
 *          - JSON value is NULL, DB value is not NULL                        *
 *                                                                            *
 ******************************************************************************/
static int	compare_nth_field(const TRX_FIELD **fields, const char *rec_data, int n, const char *str, int is_null,
		int *last_n, size_t *last_pos)
{
	int		i = *last_n, null_in_db = 0;
	const char	*p = rec_data + *last_pos, *field_start = NULL;

	do	/* find starting position of the n-th field */
	{
		field_start = p;
		while ('\0' != *p++)
			;

		null_in_db = 0;

		if (0 == (fields[i++]->flags & TRX_NOTNULL))	/* field could be NULL */
		{
			if ('\2' == *p && (rec_data == p - 1 || '\0' == *(p - 2) || '\1' == *(p - 2) ||
					'\2' == *(p - 2)))	/* field value is NULL */
			{
				null_in_db = 1;
				p++;
			}
			else if ('\1' == *p)
			{
				p++;
			}
			else
			{
				THIS_SHOULD_NEVER_HAPPEN;
				*last_n = 0;
				*last_pos = 0;
				return 1;
			}
		}
	}
	while (n >= i);

	*last_n = i;				/* preserve number of field and its start position */
	*last_pos = (size_t)(p - rec_data);	/* across calls to avoid searching from start */

	if (0 == is_null)	/* value in JSON is not NULL */
	{
		if (0 == null_in_db)
			return strcmp(field_start, str);
		else
			return 1;
	}
	else
	{
		if ('\0' == *str)
		{
			if (1 == null_in_db)
				return 0;	/* fields are "equal" - both contain NULL */
			else
				return 1;
		}
		else
		{
			THIS_SHOULD_NEVER_HAPPEN;
			*last_n = 0;
			*last_pos = 0;
			return 1;
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: process_proxyconfig_table                                        *
 *                                                                            *
 * Purpose: update configuration table                                        *
 *                                                                            *
 * Parameters: ...                                                            *
 *             del - [OUT] ids of the removed records that must be deleted    *
 *                         from database                                      *
 *                                                                            *
 * Return value: SUCCEED - processed successfully                             *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 ******************************************************************************/
static int	process_proxyconfig_table(const TRX_TABLE *table, struct trx_json_parse *jp_obj,
		trx_vector_uint64_t *del, char **error)
{
	int			f, fields_count, ret = FAIL, id_field_nr = 0, move_out = 0,
				move_field_nr = 0;
	const TRX_FIELD		*fields[TRX_MAX_FIELDS];
	struct trx_json_parse	jp_data, jp_row;
	const char		*p, *pf;
	trx_uint64_t		recid, *p_recid = NULL;
	trx_vector_uint64_t	ins, moves, availability_hostids;
	char			*buf = NULL, *esc, *sql = NULL, *recs = NULL;
	size_t			sql_alloc = 4 * TRX_KIBIBYTE, sql_offset,
				recs_alloc = 20 * TRX_KIBIBYTE, recs_offset = 0,
				buf_alloc = 0;
	DB_RESULT		result;
	DB_ROW			row;
	trx_hashset_t		h_id_offsets, h_del;
	trx_hashset_iter_t	iter;
	trx_id_offset_t		id_offset, *p_id_offset = NULL;
	trx_db_insert_t		db_insert;
	trx_vector_ptr_t	values;
	static trx_vector_ptr_t	skip_fields, availability_fields;
	static const TRX_TABLE	*table_items, *table_hosts;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() table:'%s'", __func__, table->table);

	/************************************************************************************/
	/* T1. RECEIVED JSON (jp_obj) DATA FORMAT                                           */
	/************************************************************************************/
	/* Line |                  Data                     | Corresponding structure in DB */
	/* -----+-------------------------------------------+------------------------------ */
	/*   1  | {                                         |                               */
	/*   2  |         "hosts": {                        | first table                   */
	/*   3  |                 "fields": [               | list of table's columns       */
	/*   4  |                         "hostid",         | first column                  */
	/*   5  |                         "host",           | second column                 */
	/*   6  |                         ...               | ...columns                    */
	/*   7  |                 ],                        |                               */
	/*   8  |                 "data": [                 | the table data                */
	/*   9  |                         [                 | first entry                   */
	/*  10  |                               1,          | value for first column        */
	/*  11  |                               "trx01",    | value for second column       */
	/*  12  |                               ...         | ...values                     */
	/*  13  |                         ],                |                               */
	/*  14  |                         [                 | second entry                  */
	/*  15  |                               2,          | value for first column        */
	/*  16  |                               "trx02",    | value for second column       */
	/*  17  |                               ...         | ...values                     */
	/*  18  |                         ],                |                               */
	/*  19  |                         ...               | ...entries                    */
	/*  20  |                 ]                         |                               */
	/*  21  |         },                                |                               */
	/*  22  |         "items": {                        | second table                  */
	/*  23  |                 ...                       | ...                           */
	/*  24  |         },                                |                               */
	/*  25  |         ...                               | ...tables                     */
	/*  26  | }                                         |                               */
	/************************************************************************************/

	if (NULL == table_items)
	{
		table_items = DBget_table("items");

		/* do not update existing lastlogsize and mtime fields */
		trx_vector_ptr_create(&skip_fields);
		trx_vector_ptr_append(&skip_fields, (void *)DBget_field(table_items, "lastlogsize"));
		trx_vector_ptr_append(&skip_fields, (void *)DBget_field(table_items, "mtime"));
		trx_vector_ptr_sort(&skip_fields, TRX_DEFAULT_PTR_COMPARE_FUNC);
	}

	if (NULL == table_hosts)
	{
		table_hosts = DBget_table("hosts");

		/* do not update existing lastlogsize and mtime fields */
		trx_vector_ptr_create(&availability_fields);
		trx_vector_ptr_append(&availability_fields, (void *)DBget_field(table_hosts, "available"));
		trx_vector_ptr_append(&availability_fields, (void *)DBget_field(table_hosts, "snmp_available"));
		trx_vector_ptr_append(&availability_fields, (void *)DBget_field(table_hosts, "ipmi_available"));
		trx_vector_ptr_append(&availability_fields, (void *)DBget_field(table_hosts, "jmx_available"));
		trx_vector_ptr_sort(&availability_fields, TRX_DEFAULT_PTR_COMPARE_FUNC);
	}

	/* get table columns (line 3 in T1) */
	if (FAIL == trx_json_brackets_by_name(jp_obj, "fields", &jp_data))
	{
		*error = trx_strdup(*error, trx_json_strerror());
		goto out;
	}

	p = NULL;
	/* iterate column names (lines 4-6 in T1) */
	for (fields_count = 0; NULL != (p = trx_json_next_value_dyn(&jp_data, p, &buf, &buf_alloc, NULL)); fields_count++)
	{
		if (NULL == (fields[fields_count] = DBget_field(table, buf)))
		{
			*error = trx_dsprintf(*error, "invalid field name \"%s.%s\"", table->table, buf);
			goto out;
		}

		if (0 == (fields[fields_count]->flags & TRX_PROXY) &&
				(0 != strcmp(table->recid, buf) || TRX_TYPE_ID != fields[fields_count]->type))
		{
			*error = trx_dsprintf(*error, "unexpected field \"%s.%s\"", table->table, buf);
			goto out;
		}
	}

	if (0 == fields_count)
	{
		*error = trx_dsprintf(*error, "empty list of field names");
		goto out;
	}

	/* get the entries (line 8 in T1) */
	if (FAIL == trx_json_brackets_by_name(jp_obj, TRX_PROTO_TAG_DATA, &jp_data))
	{
		*error = trx_strdup(*error, trx_json_strerror());
		goto out;
	}

	/* all records will be stored in one large string */
	recs = (char *)trx_malloc(recs, recs_alloc);

	/* hash set as index for fast access to records via IDs */
	trx_hashset_create(&h_id_offsets, 10000, id_offset_hash_func, id_offset_compare_func);

	/* a hash set as a list for finding records to be deleted */
	trx_hashset_create(&h_del, 10000, TRX_DEFAULT_UINT64_HASH_FUNC, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	sql = (char *)trx_malloc(sql, sql_alloc);

	sql_offset = 0;
	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select ");

	/* make a string with a list of fields for SELECT */
	for (f = 0; f < fields_count; f++)
	{
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, fields[f]->name);
		trx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ',');
	}

	sql_offset--;
	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " from ");
	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, table->table);

	/* Find a number of the ID field. Usually the 1st field. */
	id_field_nr = find_field_by_name(fields, fields_count, table->recid);

	/* select all existing records */
	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(recid, row[id_field_nr]);

		id_offset.id = recid;
		id_offset.offset = recs_offset;

		trx_hashset_insert(&h_id_offsets, &id_offset, sizeof(id_offset));
		trx_hashset_insert(&h_del, &recid, sizeof(recid));

		remember_record(fields, fields_count, &recs, &recs_alloc, &recs_offset, row);
	}
	DBfree_result(result);

	/* these tables have unique indices, need special preparation to avoid conflicts during inserts/updates */
	if (0 == strcmp("globalmacro", table->table))
	{
		move_out = 1;
		move_field_nr = find_field_by_name(fields, fields_count, "macro");
	}
	else if (0 == strcmp("hosts_templates", table->table))
	{
		move_out = 1;
		move_field_nr = find_field_by_name(fields, fields_count, "templateid");
	}
	else if (0 == strcmp("hostmacro", table->table))
	{
		move_out = 1;
		move_field_nr = find_field_by_name(fields, fields_count, "macro");
	}
	else if (0 == strcmp("items", table->table))
	{
		move_out = 1;
		move_field_nr = find_field_by_name(fields, fields_count, "key_");
	}
	else if (0 == strcmp("drules", table->table))
	{
		move_out = 1;
		move_field_nr = find_field_by_name(fields, fields_count, "name");
	}
	else if (0 == strcmp("regexps", table->table))
	{
		move_out = 1;
		move_field_nr = find_field_by_name(fields, fields_count, "name");
	}
	else if (0 == strcmp("httptest", table->table))
	{
		move_out = 1;
		move_field_nr = find_field_by_name(fields, fields_count, "name");
	}

	trx_vector_uint64_create(&ins);

	if (1 == move_out)
		trx_vector_uint64_create(&moves);

	trx_vector_uint64_create(&availability_hostids);

	p = NULL;
	/* iterate the entries (lines 9, 14 and 19 in T1) */
	while (NULL != (p = trx_json_next(&jp_data, p)))
	{
		if (FAIL == trx_json_brackets_open(p, &jp_row) ||
				NULL == (pf = trx_json_next_value_dyn(&jp_row, NULL, &buf, &buf_alloc, NULL)))
		{
			*error = trx_strdup(*error, trx_json_strerror());
			goto clean2;
		}

		/* check whether we need to update existing entry or insert a new one */

		TRX_STR2UINT64(recid, buf);

		if (NULL != trx_hashset_search(&h_del, &recid))
		{
			trx_hashset_remove(&h_del, &recid);

			if (1 == move_out)
			{
				int		last_n = 0;
				size_t		last_pos = 0;
				trx_json_type_t	type;

				/* locate a copy of this record as found in database */
				id_offset.id = recid;
				if (NULL == (p_id_offset = (trx_id_offset_t *)trx_hashset_search(&h_id_offsets, &id_offset)))
				{
					THIS_SHOULD_NEVER_HAPPEN;
					goto clean2;
				}

				/* find the field requiring special preprocessing in JSON record */
				f = 1;
				while (NULL != (pf = trx_json_next_value_dyn(&jp_row, pf, &buf, &buf_alloc, &type)))
				{
					/* parse values for the entry (lines 10-12 in T1) */

					if (fields_count == f)
					{
						*error = trx_dsprintf(*error, "invalid number of fields \"%.*s\"",
								(int)(jp_row.end - jp_row.start + 1), jp_row.start);
						goto clean2;
					}

					if (move_field_nr == f)
						break;
					f++;
				}

				if (0 != compare_nth_field(fields, recs + p_id_offset->offset, move_field_nr, buf,
						(TRX_JSON_TYPE_NULL == type), &last_n, &last_pos))
				{
					trx_vector_uint64_append(&moves, recid);
				}
			}
		}
		else
			trx_vector_uint64_append(&ins, recid);
	}

	/* copy IDs of records to be deleted from hash set to vector */
	trx_hashset_iter_reset(&h_del, &iter);
	while (NULL != (p_recid = (uint64_t *)trx_hashset_iter_next(&iter)))
		trx_vector_uint64_append(del, *p_recid);
	trx_vector_uint64_sort(del, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	trx_vector_uint64_sort(&ins, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	if (1 == move_out)
	{
		/* special preprocessing for 'hosts_templates' table to eliminate conflicts */
		/* in the 'hostid, templateid' unique index */
		if (0 == strcmp("hosts_templates", table->table))
		{
			/* Making the 'hostid, templateid' combination unique to avoid collisions when new records */
			/* are inserted and existing ones are updated is a bit complex. Let's take a simpler approach */
			/* - delete affected old records and insert the new ones. */
			if (0 != moves.values_num)
			{
				trx_vector_uint64_append_array(&ins, moves.values, moves.values_num);
				trx_vector_uint64_sort(&ins, TRX_DEFAULT_UINT64_COMPARE_FUNC);
				trx_vector_uint64_append_array(del, moves.values, moves.values_num);
				trx_vector_uint64_sort(del, TRX_DEFAULT_UINT64_COMPARE_FUNC);
			}

			if (0 != del->values_num)
			{
				sql_offset = 0;
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "delete from %s where", table->table);
				DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, table->recid, del->values,
						del->values_num);

				if (TRX_DB_OK > DBexecute("%s", sql))
					goto clean2;

				trx_vector_uint64_clear(del);
			}
		}
		else
		{
			/* force index field update for removed records to avoid potential conflicts */
			if (0 != del->values_num)
				trx_vector_uint64_append_array(&moves, del->values, del->values_num);

			/* special preprocessing for 'globalmacro', 'hostmacro', 'items', 'drules', 'regexps' and  */
			/* 'httptest' tables to eliminate conflicts in the 'macro', 'hostid,macro', 'hostid,key_', */
			/* 'name', 'name' and 'hostid,name' unique indices */
			if (0 < moves.values_num)
			{
				sql_offset = 0;
#ifdef HAVE_MYSQL
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
						"update %s set %s=concat('#',%s) where",
						table->table, fields[move_field_nr]->name, table->recid);
#else
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update %s set %s='#'||%s where",
						table->table, fields[move_field_nr]->name, table->recid);
#endif
				trx_vector_uint64_sort(&moves, TRX_DEFAULT_UINT64_COMPARE_FUNC);
				DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, table->recid, moves.values,
						moves.values_num);

				if (TRX_DB_OK > DBexecute("%s", sql))
					goto clean2;
			}
		}
	}

	/* apply insert operations */

	if (0 != ins.values_num)
	{
		trx_vector_ptr_create(&values);
		trx_db_insert_prepare_dyn(&db_insert, table, fields, fields_count);

		p = NULL;
		/* iterate the entries (lines 9, 14 and 19 in T1) */
		while (NULL != (p = trx_json_next(&jp_data, p)))
		{
			trx_json_type_t	type;
			trx_db_value_t	*value;

			if (FAIL == trx_json_brackets_open(p, &jp_row))
			{
				*error = trx_dsprintf(*error, "invalid data format: %s", trx_json_strerror());
				goto clean;
			}

			pf = trx_json_next_value_dyn(&jp_row, NULL, &buf, &buf_alloc, NULL);

			/* check whether we need to insert a new entry or update an existing one */
			TRX_STR2UINT64(recid, buf);
			if (FAIL == trx_vector_uint64_bsearch(&ins, recid, TRX_DEFAULT_UINT64_COMPARE_FUNC))
				continue;

			/* add the id field */
			value = (trx_db_value_t *)trx_malloc(NULL, sizeof(trx_db_value_t));
			value->ui64 = recid;
			trx_vector_ptr_append(&values, value);

			/* add the rest of fields */
			for (f = 1; NULL != (pf = trx_json_next_value_dyn(&jp_row, pf, &buf, &buf_alloc, &type));
					f++)
			{
				if (f == fields_count)
				{
					*error = trx_dsprintf(*error, "invalid number of fields \"%.*s\"",
							(int)(jp_row.end - jp_row.start + 1), jp_row.start);
					goto clean;
				}

				if (TRX_JSON_TYPE_NULL == type && 0 != (fields[f]->flags & TRX_NOTNULL))
				{
					*error = trx_dsprintf(*error, "column \"%s.%s\" cannot be null",
							table->table, fields[f]->name);
					goto clean;
				}

				value = (trx_db_value_t *)trx_malloc(NULL, sizeof(trx_db_value_t));

				switch (fields[f]->type)
				{
					case TRX_TYPE_INT:
						value->i32 = atoi(buf);
						break;
					case TRX_TYPE_UINT:
						TRX_STR2UINT64(value->ui64, buf);
						break;
					case TRX_TYPE_ID:
						if (TRX_JSON_TYPE_NULL != type)
							TRX_STR2UINT64(value->ui64, buf);
						else
							value->ui64 = 0;
						break;
					case TRX_TYPE_FLOAT:
						value->dbl = atof(buf);
						break;
					case TRX_TYPE_CHAR:
					case TRX_TYPE_TEXT:
					case TRX_TYPE_SHORTTEXT:
					case TRX_TYPE_LONGTEXT:
						value->str = trx_strdup(NULL, buf);
						break;
					default:
						*error = trx_dsprintf(*error, "unsupported field type %d in \"%s.%s\"",
								(int)fields[f]->type, table->table, fields[f]->name);
						trx_free(value);
						goto clean;

				}

				trx_vector_ptr_append(&values, value);
			}

			trx_db_insert_add_values_dyn(&db_insert, (const trx_db_value_t **)values.values,
					values.values_num);

			for (f = 0; f < fields_count; f++)
			{
				switch (fields[f]->type)
				{
					case TRX_TYPE_CHAR:
					case TRX_TYPE_TEXT:
					case TRX_TYPE_SHORTTEXT:
					case TRX_TYPE_LONGTEXT:
						value = (trx_db_value_t *)values.values[f];
						trx_free(value->str);
				}
			}
			trx_vector_ptr_clear_ext(&values, trx_ptr_free);

			if (f != fields_count)
			{
				*error = trx_dsprintf(*error, "invalid number of fields \"%.*s\"",
						(int)(jp_row.end - jp_row.start + 1), jp_row.start);
				goto clean;
			}
		}

		if (FAIL == trx_db_insert_execute(&db_insert))
			goto clean;
	}

	/* apply update operations */

	sql_offset = 0;
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	p = NULL;
	/* iterate the entries (lines 9, 14 and 19 in T1) */
	while (NULL != (p = trx_json_next(&jp_data, p)))
	{
		int		rec_differ = 0;	/* how many fields differ */
		int		last_n = 0;
		size_t		tmp_offset = sql_offset, last_pos = 0;
		trx_json_type_t	type;

		if (FAIL == trx_json_brackets_open(p, &jp_row))
		{
			*error = trx_dsprintf(*error, "invalid data format: %s", trx_json_strerror());
			goto clean;
		}

		pf = trx_json_next_value_dyn(&jp_row, NULL, &buf, &buf_alloc, NULL);

		/* check whether we need to insert a new entry or update an existing one */
		TRX_STR2UINT64(recid, buf);
		if (FAIL != trx_vector_uint64_bsearch(&ins, recid, TRX_DEFAULT_UINT64_COMPARE_FUNC))
			continue;


		if (1 == fields_count)	/* only primary key given, no update needed */
			continue;

		/* locate a copy of this record as found in database */
		id_offset.id = recid;
		if (NULL == (p_id_offset = (trx_id_offset_t *)trx_hashset_search(&h_id_offsets, &id_offset)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			goto clean;
		}

		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update %s set ", table->table);

		for (f = 1; NULL != (pf = trx_json_next_value_dyn(&jp_row, pf, &buf, &buf_alloc, &type));
				f++)
		{
			int	field_differ = 1;

			/* parse values for the entry (lines 10-12 in T1) */

			if (f == fields_count)
			{
				*error = trx_dsprintf(*error, "invalid number of fields \"%.*s\"",
						(int)(jp_row.end - jp_row.start + 1), jp_row.start);
				goto clean;
			}

			if (TRX_JSON_TYPE_NULL == type && 0 != (fields[f]->flags & TRX_NOTNULL))
			{
				*error = trx_dsprintf(*error, "column \"%s.%s\" cannot be null",
						table->table, fields[f]->name);
				goto clean;
			}

			/* do not update existing lastlogsize and mtime fields */
			if (FAIL != trx_vector_ptr_bsearch(&skip_fields, fields[f],
					TRX_DEFAULT_PTR_COMPARE_FUNC))
			{
				continue;
			}

			if (0 == (field_differ = compare_nth_field(fields, recs + p_id_offset->offset, f, buf,
					(TRX_JSON_TYPE_NULL == type), &last_n, &last_pos)))
			{
				continue;
			}

			if (table == table_hosts && FAIL != trx_vector_ptr_bsearch(&availability_fields,
					fields[f], TRX_DEFAULT_PTR_COMPARE_FUNC))
			{
				/* host availability on server differs from local (proxy) availability - */
				/* reset availability timestamp to re-send availability data to server   */
				trx_vector_uint64_append(&availability_hostids, recid);
				continue;
			}

			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%s=", fields[f]->name);
			rec_differ++;

			if (TRX_JSON_TYPE_NULL == type)
			{
				trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "null,");
				continue;
			}

			switch (fields[f]->type)
			{
				case TRX_TYPE_INT:
				case TRX_TYPE_UINT:
				case TRX_TYPE_ID:
				case TRX_TYPE_FLOAT:
					trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%s,", buf);
					break;
				default:
					esc = DBdyn_escape_string(buf);
					trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "'%s',", esc);
					trx_free(esc);
			}
		}

		if (f != fields_count)
		{
			*error = trx_dsprintf(*error, "invalid number of fields \"%.*s\"",
					(int)(jp_row.end - jp_row.start + 1), jp_row.start);
			goto clean;
		}

		sql_offset--;

		if (0 != rec_differ)
		{
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where %s=" TRX_FS_UI64 ";\n",
					table->recid, recid);

			if (SUCCEED != DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset))
				goto clean;
		}
		else
		{
			sql_offset = tmp_offset;	/* discard this update, all fields are the same */
			*(sql + sql_offset) = '\0';
		}
	}

	if (16 < sql_offset)	/* in ORACLE always present begin..end; */
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (TRX_DB_OK > DBexecute("%s", sql))
			goto clean;
	}

	/* delete operations are performed by the caller using the returned del vector */

	if (0 != availability_hostids.values_num)
	{
		trx_vector_uint64_sort(&availability_hostids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
		trx_vector_uint64_uniq(&availability_hostids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
		DCtouch_hosts_availability(&availability_hostids);
	}

	ret = SUCCEED;
clean:
	if (0 != ins.values_num)
	{
		trx_db_insert_clean(&db_insert);
		trx_vector_ptr_destroy(&values);
	}
clean2:
	trx_hashset_destroy(&h_id_offsets);
	trx_hashset_destroy(&h_del);
	trx_vector_uint64_destroy(&availability_hostids);
	trx_vector_uint64_destroy(&ins);
	if (1 == move_out)
		trx_vector_uint64_destroy(&moves);
	trx_free(sql);
	trx_free(recs);
out:
	trx_free(buf);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_proxyconfig                                              *
 *                                                                            *
 * Purpose: update configuration                                              *
 *                                                                            *
 ******************************************************************************/
void	process_proxyconfig(struct trx_json_parse *jp_data)
{
	typedef struct
	{
		const TRX_TABLE		*table;
		trx_vector_uint64_t	ids;
	}
	table_ids_t;

	char			buf[TRX_TABLENAME_LEN_MAX];
	const char		*p = NULL;
	struct trx_json_parse	jp_obj;
	char			*error = NULL;
	int			i, ret = SUCCEED;

	table_ids_t		*table_ids;
	trx_vector_ptr_t	tables_proxy;
	const TRX_TABLE		*table;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&tables_proxy);

	DBbegin();

	/* iterate the tables (lines 2, 22 and 25 in T1) */
	while (NULL != (p = trx_json_pair_next(jp_data, p, buf, sizeof(buf))) && SUCCEED == ret)
	{
		if (FAIL == trx_json_brackets_open(p, &jp_obj))
		{
			error = trx_strdup(error, trx_json_strerror());
			ret = FAIL;
			break;
		}

		if (NULL == (table = DBget_table(buf)))
		{
			error = trx_dsprintf(error, "invalid table name \"%s\"", buf);
			ret = FAIL;
			break;
		}

		table_ids = (table_ids_t *)trx_malloc(NULL, sizeof(table_ids_t));
		table_ids->table = table;
		trx_vector_uint64_create(&table_ids->ids);
		trx_vector_ptr_append(&tables_proxy, table_ids);

		ret = process_proxyconfig_table(table, &jp_obj, &table_ids->ids, &error);
	}

	if (SUCCEED == ret)
	{
		char	*sql = NULL;
		size_t	sql_alloc = 512, sql_offset = 0;

		sql = (char *)trx_malloc(sql, sql_alloc * sizeof(char));

		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

		for (i = tables_proxy.values_num - 1; 0 <= i; i--)
		{
			table_ids = (table_ids_t *)tables_proxy.values[i];

			if (0 == table_ids->ids.values_num)
				continue;

			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "delete from %s where",
					table_ids->table->table);
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, table_ids->table->recid,
					table_ids->ids.values, table_ids->ids.values_num);
			trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
		}

		if (sql_offset > 16)	/* in ORACLE always present begin..end; */
		{
			DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

			if (TRX_DB_OK > DBexecute("%s", sql))
				ret = FAIL;
		}

		trx_free(sql);
	}

	for (i = 0; i < tables_proxy.values_num; i++)
	{
		table_ids = (table_ids_t *)tables_proxy.values[i];

		trx_vector_uint64_destroy(&table_ids->ids);
		trx_free(table_ids);
	}
	trx_vector_ptr_destroy(&tables_proxy);

	if (SUCCEED != (ret = DBend(ret)))
	{
		treegix_log(LOG_LEVEL_ERR, "failed to update local proxy configuration copy: %s",
				(NULL == error ? "database error" : error));
	}
	else
	{
		DCsync_configuration(TRX_DBSYNC_UPDATE);
		DCupdate_hosts_availability();
	}

	trx_free(error);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: get_host_availability_data                                       *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - no host availability has been changed                *
 *                                                                            *
 ******************************************************************************/
int	get_host_availability_data(struct trx_json *json, int *ts)
{
	int				i, j, ret = FAIL;
	trx_vector_ptr_t		hosts;
	trx_host_availability_t		*ha;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&hosts);

	if (SUCCEED != DCget_hosts_availability(&hosts, ts))
		goto out;

	trx_json_addarray(json, TRX_PROTO_TAG_HOST_AVAILABILITY);

	for (i = 0; i < hosts.values_num; i++)
	{
		ha = (trx_host_availability_t *)hosts.values[i];

		trx_json_addobject(json, NULL);
		trx_json_adduint64(json, TRX_PROTO_TAG_HOSTID, ha->hostid);

		for (j = 0; j < TRX_AGENT_MAX; j++)
		{
			trx_json_adduint64(json, availability_tag_available[j], ha->agents[j].available);
			trx_json_addstring(json, availability_tag_error[j], ha->agents[j].error, TRX_JSON_TYPE_STRING);
		}

		trx_json_close(json);
	}

	trx_json_close(json);

	ret = SUCCEED;
out:
	trx_vector_ptr_clear_ext(&hosts, (trx_mem_free_func_t)trx_host_availability_free);
	trx_vector_ptr_destroy(&hosts);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_host_availability_contents                               *
 *                                                                            *
 * Purpose: parses host availability data contents and processes it           *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
static int	process_host_availability_contents(struct trx_json_parse *jp_data, char **error)
{
	trx_uint64_t		hostid;
	struct trx_json_parse	jp_row;
	const char		*p = NULL;
	char			*tmp = NULL;
	size_t			tmp_alloc = 129;
	trx_host_availability_t	*ha = NULL;
	trx_vector_ptr_t	hosts;
	int			i, ret;

	tmp = (char *)trx_malloc(NULL, tmp_alloc);

	trx_vector_ptr_create(&hosts);

	while (NULL != (p = trx_json_next(jp_data, p)))	/* iterate the host entries */
	{
		if (SUCCEED != (ret = trx_json_brackets_open(p, &jp_row)))
		{
			*error = trx_strdup(*error, trx_json_strerror());
			goto out;
		}

		if (SUCCEED != (ret = trx_json_value_by_name_dyn(&jp_row, TRX_PROTO_TAG_HOSTID, &tmp, &tmp_alloc)))
		{
			*error = trx_strdup(*error, trx_json_strerror());
			goto out;
		}

		if (SUCCEED != (ret = is_uint64(tmp, &hostid)))
		{
			*error = trx_strdup(*error, "hostid is not a valid numeric");
			goto out;
		}

		ha = (trx_host_availability_t *)trx_malloc(NULL, sizeof(trx_host_availability_t));
		trx_host_availability_init(ha, hostid);

		for (i = 0; i < TRX_AGENT_MAX; i++)
		{
			if (SUCCEED != trx_json_value_by_name_dyn(&jp_row, availability_tag_available[i], &tmp,
					&tmp_alloc))
			{
				continue;
			}

			ha->agents[i].available = atoi(tmp);
			ha->agents[i].flags |= TRX_FLAGS_AGENT_STATUS_AVAILABLE;
		}

		for (i = 0; i < TRX_AGENT_MAX; i++)
		{
			if (SUCCEED != trx_json_value_by_name_dyn(&jp_row, availability_tag_error[i], &tmp, &tmp_alloc))
				continue;

			ha->agents[i].error = trx_strdup(NULL, tmp);
			ha->agents[i].flags |= TRX_FLAGS_AGENT_STATUS_ERROR;
		}

		if (SUCCEED != (ret = trx_host_availability_is_set(ha)))
		{
			trx_free(ha);
			*error = trx_dsprintf(*error, "no availability data for \"hostid\":" TRX_FS_UI64, hostid);
			goto out;
		}

		trx_vector_ptr_append(&hosts, ha);
	}

	if (0 < hosts.values_num && SUCCEED == DCset_hosts_availability(&hosts))
	{
		char	*sql = NULL;
		size_t	sql_alloc = 4 * TRX_KIBIBYTE, sql_offset = 0;

		sql = (char *)trx_malloc(sql, sql_alloc);

		DBbegin();
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

		for (i = 0; i < hosts.values_num; i++)
		{
			if (SUCCEED != trx_sql_add_host_availability(&sql, &sql_alloc, &sql_offset,
					(trx_host_availability_t *)hosts.values[i]))
			{
				continue;
			}

			trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
			DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
		}

		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (16 < sql_offset)
			DBexecute("%s", sql);

		DBcommit();

		trx_free(sql);
	}

	ret = SUCCEED;
out:
	trx_vector_ptr_clear_ext(&hosts, (trx_mem_free_func_t)trx_host_availability_free);
	trx_vector_ptr_destroy(&hosts);

	trx_free(tmp);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_host_availability                                        *
 *                                                                            *
 * Purpose: update proxy hosts availability                                   *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
int	process_host_availability(struct trx_json_parse *jp, char **error)
{
	struct trx_json_parse	jp_data;
	int			ret;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != (ret = trx_json_brackets_by_name(jp, TRX_PROTO_TAG_DATA, &jp_data)))
	{
		*error = trx_strdup(*error, trx_json_strerror());
		goto out;
	}

	if (SUCCEED == trx_json_object_is_empty(&jp_data))
		goto out;

	ret = process_host_availability_contents(&jp_data, error);

out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: proxy_get_lastid                                                 *
 *                                                                            *
 ******************************************************************************/
static void	proxy_get_lastid(const char *table_name, const char *lastidfield, trx_uint64_t *lastid)
{
	DB_RESULT	result;
	DB_ROW		row;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() field:'%s.%s'", __func__, table_name, lastidfield);

	result = DBselect("select nextid from ids where table_name='%s' and field_name='%s'",
			table_name, lastidfield);

	if (NULL == (row = DBfetch(result)))
		*lastid = 0;
	else
		TRX_STR2UINT64(*lastid, row[0]);
	DBfree_result(result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():" TRX_FS_UI64,	__func__, *lastid);
}

/******************************************************************************
 *                                                                            *
 * Function: proxy_set_lastid                                                 *
 *                                                                            *
 ******************************************************************************/
static void	proxy_set_lastid(const char *table_name, const char *lastidfield, const trx_uint64_t lastid)
{
	DB_RESULT	result;
	DB_ROW		row;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() [%s.%s:" TRX_FS_UI64 "]", __func__, table_name, lastidfield, lastid);

	result = DBselect("select 1 from ids where table_name='%s' and field_name='%s'",
			table_name, lastidfield);

	if (NULL == (row = DBfetch(result)))
	{
		DBexecute("insert into ids (table_name,field_name,nextid) values ('%s','%s'," TRX_FS_UI64 ")",
				table_name, lastidfield, lastid);
	}
	else
	{
		DBexecute("update ids set nextid=" TRX_FS_UI64 " where table_name='%s' and field_name='%s'",
				lastid, table_name, lastidfield);
	}
	DBfree_result(result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

void	proxy_set_hist_lastid(const trx_uint64_t lastid)
{
	proxy_set_lastid("proxy_history", "history_lastid", lastid);
}

void	proxy_set_dhis_lastid(const trx_uint64_t lastid)
{
	proxy_set_lastid(dht.table, dht.lastidfield, lastid);
}

void	proxy_set_areg_lastid(const trx_uint64_t lastid)
{
	proxy_set_lastid(areg.table, areg.lastidfield, lastid);
}

/******************************************************************************
 *                                                                            *
 * Function: proxy_get_history_data_simple                                    *
 *                                                                            *
 * Purpose: Get history data from the database.                               *
 *                                                                            *
 ******************************************************************************/
static void	proxy_get_history_data_simple(struct trx_json *j, const char *proto_tag, const trx_history_table_t *ht,
		trx_uint64_t *lastid, trx_uint64_t *id, int *records_num, int *more)
{
	size_t		offset = 0;
	int		f, records_num_last = *records_num, retries = 1;
	char		sql[MAX_STRING_LEN];
	DB_RESULT	result;
	DB_ROW		row;
	struct timespec	t_sleep = { 0, 100000000L }, t_rem;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() table:'%s'", __func__, ht->table);

	*more = TRX_PROXY_DATA_DONE;

	offset += trx_snprintf(sql + offset, sizeof(sql) - offset, "select id");

	for (f = 0; NULL != ht->fields[f].field; f++)
		offset += trx_snprintf(sql + offset, sizeof(sql) - offset, ",%s", ht->fields[f].field);
try_again:
	trx_snprintf(sql + offset, sizeof(sql) - offset, " from %s where id>" TRX_FS_UI64 " order by id",
			ht->table, *id);

	result = DBselectN(sql, TRX_MAX_HRECORDS);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(*lastid, row[0]);

		if (1 < *lastid - *id)
		{
			/* At least one record is missing. It can happen if some DB syncer process has */
			/* started but not yet committed a transaction or a rollback occurred in a DB syncer. */
			if (0 < retries--)
			{
				DBfree_result(result);
				treegix_log(LOG_LEVEL_DEBUG, "%s() " TRX_FS_UI64 " record(s) missing."
						" Waiting " TRX_FS_DBL " sec, retrying.",
						__func__, *lastid - *id - 1,
						t_sleep.tv_sec + t_sleep.tv_nsec / 1e9);
				nanosleep(&t_sleep, &t_rem);
				goto try_again;
			}
			else
			{
				treegix_log(LOG_LEVEL_DEBUG, "%s() " TRX_FS_UI64 " record(s) missing. No more retries.",
						__func__, *lastid - *id - 1);
			}
		}

		if (0 == *records_num)
			trx_json_addarray(j, proto_tag);

		trx_json_addobject(j, NULL);

		for (f = 0; NULL != ht->fields[f].field; f++)
		{
			if (NULL != ht->fields[f].default_value && 0 == strcmp(row[f + 1], ht->fields[f].default_value))
				continue;

			trx_json_addstring(j, ht->fields[f].tag, row[f + 1], ht->fields[f].jt);
		}

		(*records_num)++;

		trx_json_close(j);

		/* stop gathering data to avoid exceeding the maximum packet size */
		if (TRX_DATA_JSON_RECORD_LIMIT < j->buffer_offset)
		{
			*more = TRX_PROXY_DATA_MORE;
			break;
		}

		*id = *lastid;
	}
	DBfree_result(result);

	if (TRX_MAX_HRECORDS == *records_num - records_num_last)
		*more = TRX_PROXY_DATA_MORE;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d lastid:" TRX_FS_UI64 " more:%d size:" TRX_FS_SIZE_T,
			__func__, *records_num - records_num_last, *lastid, *more,
			(trx_fs_size_t)j->buffer_offset);
}

typedef struct
{
	trx_uint64_t	id;
	trx_uint64_t	itemid;
	trx_uint64_t	lastlogsize;
	size_t		source_offset;
	size_t		value_offset;
	int		clock;
	int		ns;
	int		timestamp;
	int		severity;
	int		logeventid;
	int		mtime;
	unsigned char	state;
	unsigned char	flags;
}
trx_history_data_t;

/******************************************************************************
 *                                                                            *
 * Function: proxy_get_history_data                                           *
 *                                                                            *
 * Purpose: read proxy history data from the database                         *
 *                                                                            *
 * Parameters: lastid             - [IN] the id of last processed proxy       *
 *                                       history record                       *
 *             data               - [IN/OUT] the proxy history data buffer    *
 *             data_alloc         - [IN/OUT] the size of proxy history data   *
 *                                           buffer                           *
 *             string_buffer      - [IN/OUT] the string buffer                *
 *             string_buffer_size - [IN/OUT] the size of string buffer        *
 *             more               - [OUT] set to TRX_PROXY_DATA_MORE if there *
 *                                        might be more data to read          *
 *                                                                            *
 * Return value: The number of records read.                                  *
 *                                                                            *
 ******************************************************************************/
static int	proxy_get_history_data(trx_uint64_t lastid, trx_history_data_t **data, size_t *data_alloc,
		char **string_buffer, size_t *string_buffer_alloc, int *more)
{

	DB_RESULT		result;
	DB_ROW			row;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0, data_num = 0;
	size_t			string_buffer_offset = 0;
	trx_uint64_t		id;
	int			retries = 1, total_retries = 10;
	struct timespec		t_sleep = { 0, 100000000L }, t_rem;
	trx_history_data_t	*hd;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() lastid:" TRX_FS_UI64, __func__, lastid);

try_again:
	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select id,itemid,clock,ns,timestamp,source,severity,"
				"value,logeventid,state,lastlogsize,mtime,flags"
			" from proxy_history"
			" where id>" TRX_FS_UI64
			" order by id",
			lastid);

	result = DBselectN(sql, TRX_MAX_HRECORDS - data_num);

	trx_free(sql);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(id, row[0]);

		if (1 < id - lastid)
		{
			/* At least one record is missing. It can happen if some DB syncer process has */
			/* started but not yet committed a transaction or a rollback occurred in a DB syncer. */
			if (0 < retries--)
			{
				/* limit the number of total retries to avoid being stuck */
				/* in history full of 'holes' for a long time             */
				if (0 >= total_retries--)
					break;

				DBfree_result(result);
				treegix_log(LOG_LEVEL_DEBUG, "%s() " TRX_FS_UI64 " record(s) missing."
						" Waiting " TRX_FS_DBL " sec, retrying.",
						__func__, id - lastid - 1,
						t_sleep.tv_sec + t_sleep.tv_nsec / 1e9);
				nanosleep(&t_sleep, &t_rem);
				goto try_again;
			}
			else
			{
				treegix_log(LOG_LEVEL_DEBUG, "%s() " TRX_FS_UI64 " record(s) missing. No more retries.",
						__func__, id - lastid - 1);
			}
		}

		retries = 1;

		if (*data_alloc == data_num)
		{
			*data_alloc *= 2;
			*data = (trx_history_data_t *)trx_realloc(*data, sizeof(trx_history_data_t) * *data_alloc);
		}

		hd = *data + data_num++;
		hd->id = id;
		TRX_STR2UINT64(hd->itemid, row[1]);
		TRX_STR2UCHAR(hd->flags, row[12]);
		hd->clock = atoi(row[2]);
		hd->ns = atoi(row[3]);

		if (PROXY_HISTORY_FLAG_NOVALUE != (hd->flags & PROXY_HISTORY_MASK_NOVALUE))
		{
			TRX_STR2UCHAR(hd->state, row[9]);

			if (0 == (hd->flags & PROXY_HISTORY_FLAG_NOVALUE))
			{
				size_t	len1, len2;

				hd->timestamp = atoi(row[4]);
				hd->severity = atoi(row[6]);
				hd->logeventid = atoi(row[8]);

				len1 = strlen(row[5]) + 1;
				len2 = strlen(row[7]) + 1;

				if (*string_buffer_alloc < string_buffer_offset + len1 + len2)
				{
					while (*string_buffer_alloc < string_buffer_offset + len1 + len2)
						*string_buffer_alloc += TRX_KIBIBYTE;

					*string_buffer = (char *)trx_realloc(*string_buffer, *string_buffer_alloc);
				}

				hd->source_offset = string_buffer_offset;
				memcpy(*string_buffer + hd->source_offset, row[5], len1);
				string_buffer_offset += len1;

				hd->value_offset = string_buffer_offset;
				memcpy(*string_buffer + hd->value_offset, row[7], len2);
				string_buffer_offset += len2;
			}

			if (0 != (hd->flags & PROXY_HISTORY_FLAG_META))
			{
				TRX_STR2UINT64(hd->lastlogsize, row[10]);
				hd->mtime = atoi(row[11]);
			}
		}

		lastid = id;
	}
	DBfree_result(result);

	if (TRX_MAX_HRECORDS != data_num && 1 == retries)
		*more = TRX_PROXY_DATA_DONE;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() data_num:" TRX_FS_SIZE_T, __func__, data_num);

	return data_num;
}

/******************************************************************************
 *                                                                            *
 * Function: proxy_add_hist_data                                              *
 *                                                                            *
 * Purpose: add history records to output json                                *
 *                                                                            *
 * Parameters: j             - [IN] the json output buffer                    *
 *             records_num   - [IN] the total number of records added         *
 *             dc_items      - [IN] the item configuration data               *
 *             errcodes      - [IN] the item configuration status codes       *
 *             records       - [IN] the records to add                        *
 *             string_buffer - [IN] the string buffer holding string values   *
 *             lastid        - [OUT] the id of last added record              *
 *                                                                            *
 * Return value: The total number of records added.                           *
 *                                                                            *
 ******************************************************************************/
static int	proxy_add_hist_data(struct trx_json *j, int records_num, const DC_ITEM *dc_items, const int *errcodes,
		const trx_vector_ptr_t *records, const char *string_buffer, trx_uint64_t *lastid)
{
	int				i;
	const trx_history_data_t	*hd;

	for (i = records->values_num - 1; i >= 0; i--)
	{
		hd = (const trx_history_data_t *)records->values[i];
		*lastid = hd->id;

		if (SUCCEED != errcodes[i])
			continue;

		if (ITEM_STATUS_ACTIVE != dc_items[i].status)
			continue;

		if (HOST_STATUS_MONITORED != dc_items[i].host.status)
			continue;

		if (PROXY_HISTORY_FLAG_NOVALUE == (hd->flags & PROXY_HISTORY_MASK_NOVALUE))
		{
			if (SUCCEED != trx_is_counted_in_item_queue(dc_items[i].type, dc_items[i].key_orig))
				continue;
		}

		if (0 == records_num)
			trx_json_addarray(j, TRX_PROTO_TAG_HISTORY_DATA);

		trx_json_addobject(j, NULL);
		trx_json_adduint64(j, TRX_PROTO_TAG_ID, hd->id);
		trx_json_adduint64(j, TRX_PROTO_TAG_ITEMID, hd->itemid);
		trx_json_adduint64(j, TRX_PROTO_TAG_CLOCK, hd->clock);
		trx_json_adduint64(j, TRX_PROTO_TAG_NS, hd->ns);

		if (PROXY_HISTORY_FLAG_NOVALUE != (hd->flags & PROXY_HISTORY_MASK_NOVALUE))
		{
			if (ITEM_STATE_NORMAL != hd->state)
				trx_json_adduint64(j, TRX_PROTO_TAG_STATE, hd->state);

			if (0 == (hd->flags & PROXY_HISTORY_FLAG_NOVALUE))
			{
				if (0 != hd->timestamp)
					trx_json_adduint64(j, TRX_PROTO_TAG_LOGTIMESTAMP, hd->timestamp);

				if ('\0' != string_buffer[hd->source_offset])
				{
					trx_json_addstring(j, TRX_PROTO_TAG_LOGSOURCE,
							string_buffer + hd->source_offset, TRX_JSON_TYPE_STRING);
				}

				if (0 != hd->severity)
					trx_json_adduint64(j, TRX_PROTO_TAG_LOGSEVERITY, hd->severity);

				if (0 != hd->logeventid)
					trx_json_adduint64(j, TRX_PROTO_TAG_LOGEVENTID, hd->logeventid);

				trx_json_addstring(j, TRX_PROTO_TAG_VALUE, string_buffer + hd->value_offset,
						TRX_JSON_TYPE_STRING);
			}

			if (0 != (hd->flags & PROXY_HISTORY_FLAG_META))
			{
				trx_json_adduint64(j, TRX_PROTO_TAG_LASTLOGSIZE, hd->lastlogsize);
				trx_json_adduint64(j, TRX_PROTO_TAG_MTIME, hd->mtime);
			}
		}

		trx_json_close(j);
		records_num++;

		/* stop gathering data to avoid exceeding the maximum packet size */
		if (TRX_DATA_JSON_RECORD_LIMIT < j->buffer_offset)
			break;
	}

	return records_num;
}

int	proxy_get_hist_data(struct trx_json *j, trx_uint64_t *lastid, int *more)
{
	int			records_num = 0, data_num, i, *errcodes = NULL, items_alloc = 0;
	trx_uint64_t		id;
	trx_hashset_t		itemids_added;
	trx_history_data_t	*data;
	char			*string_buffer;
	size_t			data_alloc = 16, string_buffer_alloc = TRX_KIBIBYTE;
	trx_vector_uint64_t	itemids;
	trx_vector_ptr_t	records;
	DC_ITEM			*dc_items = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&itemids);
	trx_vector_ptr_create(&records);
	data = (trx_history_data_t *)trx_malloc(NULL, data_alloc * sizeof(trx_history_data_t));
	string_buffer = (char *)trx_malloc(NULL, string_buffer_alloc);

	*more = TRX_PROXY_DATA_MORE;
	proxy_get_lastid("proxy_history", "history_lastid", &id);

	trx_hashset_create(&itemids_added, data_alloc, TRX_DEFAULT_UINT64_HASH_FUNC, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	/* get history data in batches by TRX_MAX_HRECORDS records and stop if: */
	/*   1) there are no more data to read                                  */
	/*   2) we have retrieved more than the total maximum number of records */
	/*   3) we have gathered more than half of the maximum packet size      */
	while (TRX_DATA_JSON_BATCH_LIMIT > j->buffer_offset && TRX_MAX_HRECORDS_TOTAL > records_num &&
			0 != (data_num = proxy_get_history_data(id, &data, &data_alloc, &string_buffer,
					&string_buffer_alloc, more)))
	{
		trx_vector_uint64_reserve(&itemids, data_num);
		trx_vector_ptr_reserve(&records, data_num);

		/* filter out duplicate novalue updates */
		for (i = data_num - 1; i >= 0; i--)
		{
			if (PROXY_HISTORY_FLAG_NOVALUE == (data[i].flags & PROXY_HISTORY_MASK_NOVALUE))
			{
				if (NULL != trx_hashset_search(&itemids_added, &data[i].itemid))
					continue;

				trx_hashset_insert(&itemids_added, &data[i].itemid, sizeof(data[i].itemid));
			}

			trx_vector_ptr_append(&records, &data[i]);
			trx_vector_uint64_append(&itemids, data[i].itemid);
		}

		/* append history records to json */

		if (itemids.values_num > items_alloc)
		{
			items_alloc = itemids.values_num;
			dc_items = (DC_ITEM *)trx_realloc(dc_items, items_alloc * sizeof(DC_ITEM));
			errcodes = (int *)trx_realloc(errcodes, items_alloc * sizeof(int));
		}

		DCconfig_get_items_by_itemids(dc_items, itemids.values, errcodes, itemids.values_num);

		records_num = proxy_add_hist_data(j, records_num, dc_items, errcodes, &records, string_buffer, lastid);
		DCconfig_clean_items(dc_items, errcodes, itemids.values_num);

		/* got less data than requested - either no more data to read or the history is full of */
		/* holes. In this case send retrieved data before attempting to read/wait for more data */
		if (TRX_MAX_HRECORDS > data_num)
			break;

		trx_vector_uint64_clear(&itemids);
		trx_vector_ptr_clear(&records);
		trx_hashset_clear(&itemids_added);
		id = *lastid;
	}

	if (0 != records_num)
		trx_json_close(j);

	trx_hashset_destroy(&itemids_added);

	trx_free(dc_items);
	trx_free(errcodes);
	trx_free(data);
	trx_free(string_buffer);
	trx_vector_ptr_destroy(&records);
	trx_vector_uint64_destroy(&itemids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() lastid:" TRX_FS_UI64 " records_num:%d size:~" TRX_FS_SIZE_T " more:%d",
			__func__, *lastid, records_num, j->buffer_offset, *more);

	return records_num;
}

int	proxy_get_dhis_data(struct trx_json *j, trx_uint64_t *lastid, int *more)
{
	int		records_num = 0;
	trx_uint64_t	id;

	proxy_get_lastid(dht.table, dht.lastidfield, &id);

	/* get history data in batches by TRX_MAX_HRECORDS records and stop if: */
	/*   1) there are no more data to read                                  */
	/*   2) we have retrieved more than the total maximum number of records */
	/*   3) we have gathered more than half of the maximum packet size      */
	while (TRX_DATA_JSON_BATCH_LIMIT > j->buffer_offset)
	{
		proxy_get_history_data_simple(j, TRX_PROTO_TAG_DISCOVERY_DATA, &dht, lastid, &id, &records_num, more);

		if (TRX_PROXY_DATA_DONE == *more || TRX_MAX_HRECORDS_TOTAL <= records_num)
			break;
	}

	if (0 != records_num)
		trx_json_close(j);

	return records_num;
}

int	proxy_get_areg_data(struct trx_json *j, trx_uint64_t *lastid, int *more)
{
	int		records_num = 0;
	trx_uint64_t	id;

	proxy_get_lastid(areg.table, areg.lastidfield, &id);

	/* get history data in batches by TRX_MAX_HRECORDS records and stop if: */
	/*   1) there are no more data to read                                  */
	/*   2) we have retrieved more than the total maximum number of records */
	/*   3) we have gathered more than half of the maximum packet size      */
	while (TRX_DATA_JSON_BATCH_LIMIT > j->buffer_offset)
	{
		proxy_get_history_data_simple(j, TRX_PROTO_TAG_AUTO_REGISTRATION, &areg, lastid, &id, &records_num,
				more);

		if (TRX_PROXY_DATA_DONE == *more || TRX_MAX_HRECORDS_TOTAL <= records_num)
			break;
	}

	if (0 != records_num)
		trx_json_close(j);

	return records_num;
}

void	calc_timestamp(const char *line, int *timestamp, const char *format)
{
	int		hh, mm, ss, yyyy, dd, MM;
	int		hhc = 0, mmc = 0, ssc = 0, yyyyc = 0, ddc = 0, MMc = 0;
	int		i, num;
	struct tm	tm;
	time_t		t;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	hh = mm = ss = yyyy = dd = MM = 0;

	for (i = 0; '\0' != format[i] && '\0' != line[i]; i++)
	{
		if (0 == isdigit(line[i]))
			continue;

		num = (int)line[i] - 48;

		switch ((char)format[i])
		{
			case 'h':
				hh = 10 * hh + num;
				hhc++;
				break;
			case 'm':
				mm = 10 * mm + num;
				mmc++;
				break;
			case 's':
				ss = 10 * ss + num;
				ssc++;
				break;
			case 'y':
				yyyy = 10 * yyyy + num;
				yyyyc++;
				break;
			case 'd':
				dd = 10 * dd + num;
				ddc++;
				break;
			case 'M':
				MM = 10 * MM + num;
				MMc++;
				break;
		}
	}

	treegix_log(LOG_LEVEL_DEBUG, "%s() %02d:%02d:%02d %02d/%02d/%04d", __func__, hh, mm, ss, MM, dd, yyyy);

	/* seconds can be ignored, no ssc here */
	if (0 != hhc && 0 != mmc && 0 != yyyyc && 0 != ddc && 0 != MMc)
	{
		tm.tm_sec = ss;
		tm.tm_min = mm;
		tm.tm_hour = hh;
		tm.tm_mday = dd;
		tm.tm_mon = MM - 1;
		tm.tm_year = yyyy - 1900;
		tm.tm_isdst = -1;

		if (0 < (t = mktime(&tm)))
			*timestamp = t;
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() timestamp:%d", __func__, *timestamp);
}

/******************************************************************************
 *                                                                            *
 * Function: process_item_value                                               *
 *                                                                            *
 * Purpose: processes item value depending on proxy/flags settings            *
 *                                                                            *
 * Parameters: item    - [IN] the item to process                             *
 *             result  - [IN] the item result                                 *
 *                                                                            *
 * Comments: Values gathered by server are sent to the preprocessing manager, *
 *           while values received from proxy are already preprocessed and    *
 *           must be either directly stored to history cache or sent to lld   *
 *           manager.                                                         *
 *                                                                            *
 ******************************************************************************/
static void	process_item_value(const DC_ITEM *item, AGENT_RESULT *result, trx_timespec_t *ts, char *error)
{
	if (0 == item->host.proxy_hostid)
	{
		trx_preprocess_item_value(item->itemid, item->value_type, item->flags, result, ts, item->state, error);
	}
	else
	{
		if (0 != (TRX_FLAG_DISCOVERY_RULE & item->flags))
			trx_lld_process_agent_result(item->itemid, result, ts, error);
		else
			dc_add_history(item->itemid, item->value_type, item->flags, result, ts, item->state, error);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: process_history_data_value                                       *
 *                                                                            *
 * Purpose: process single value from incoming history data                   *
 *                                                                            *
 * Parameters: item    - [IN] the item to process                             *
 *             value   - [IN] the value to process                            *
 *                                                                            *
 * Return value: SUCCEED - the value was processed successfully               *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	process_history_data_value(DC_ITEM *item, trx_agent_value_t *value)
{
	if (ITEM_STATUS_ACTIVE != item->status)
		return FAIL;

	if (HOST_STATUS_MONITORED != item->host.status)
		return FAIL;

	/* update item nextcheck during maintenance */
	if (SUCCEED == in_maintenance_without_data_collection(item->host.maintenance_status,
			item->host.maintenance_type, item->type) &&
			item->host.maintenance_from <= value->ts.sec)
	{
		return SUCCEED;
	}

	if (NULL == value->value && ITEM_STATE_NOTSUPPORTED == value->state)
	{
		THIS_SHOULD_NEVER_HAPPEN;
		return FAIL;
	}

	if (ITEM_STATE_NOTSUPPORTED == value->state ||
			(NULL != value->value && 0 == strcmp(value->value, TRX_NOTSUPPORTED)))
	{
		treegix_log(LOG_LEVEL_DEBUG, "item [%s:%s] error: %s", item->host.host, item->key_orig, value->value);

		item->state = ITEM_STATE_NOTSUPPORTED;
		process_item_value(item, NULL, &value->ts, value->value);
	}
	else
	{
		AGENT_RESULT	result;

		init_result(&result);

		if (NULL != value->value)
		{
			if (ITEM_VALUE_TYPE_LOG == item->value_type)
			{
				trx_log_t	*log;

				log = (trx_log_t *)trx_malloc(NULL, sizeof(trx_log_t));
				log->value = trx_strdup(NULL, value->value);
				trx_replace_invalid_utf8(log->value);

				if (0 == value->timestamp)
				{
					log->timestamp = 0;
					calc_timestamp(log->value, &log->timestamp, item->logtimefmt);
				}
				else
					log->timestamp = value->timestamp;

				log->logeventid = value->logeventid;
				log->severity = value->severity;

				if (NULL != value->source)
				{
					log->source = trx_strdup(NULL, value->source);
					trx_replace_invalid_utf8(log->source);
				}
				else
					log->source = NULL;

				SET_LOG_RESULT(&result, log);
			}
			else
				set_result_type(&result, ITEM_VALUE_TYPE_TEXT, value->value);
		}

		if (0 != value->meta)
			set_result_meta(&result, value->lastlogsize, value->mtime);

		if (0 != ISSET_VALUE(&result) || 0 != ISSET_META(&result))
		{
			item->state = ITEM_STATE_NORMAL;
			process_item_value(item, &result, &value->ts, NULL);
		}

		free_result(&result);
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: process_history_data                                             *
 *                                                                            *
 * Purpose: process new item values                                           *
 *                                                                            *
 * Parameters: items      - [IN] the items to process                         *
 *             values     - [IN] the item values value to process             *
 *             errcodes   - [IN/OUT] in - item configuration error code       *
 *                                      (FAIL - item/host was not found)      *
 *                                   out - value processing result            *
 *                                      (SUCCEED - processed, FAIL - error)   *
 *             values_num - [IN] the number of items/values to process        *
 *                                                                            *
 * Return value: the number of processed values                               *
 *                                                                            *
 ******************************************************************************/
int	process_history_data(DC_ITEM *items, trx_agent_value_t *values, int *errcodes, size_t values_num)
{
	size_t	i;
	int	processed_num = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (i = 0; i < values_num; i++)
	{
		if (SUCCEED != errcodes[i])
			continue;

		if (SUCCEED != process_history_data_value(&items[i], &values[i]))
		{
			/* clean failed items to avoid updating their runtime data */
			DCconfig_clean_items(&items[i], &errcodes[i], 1);
			errcodes[i] = FAIL;
			continue;
		}

		processed_num++;
	}

	if (0 < processed_num)
		trx_dc_items_update_nextcheck(items, values, errcodes, values_num);

	trx_preprocessor_flush();
	dc_flush_history();

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() processed:%d", __func__, processed_num);

	return processed_num;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_agent_values_clean                                           *
 *                                                                            *
 * Purpose: frees resources allocated to store agent values                   *
 *                                                                            *
 * Parameters: values     - [IN] the values to clean                          *
 *             values_num - [IN] the number of items in values array          *
 *                                                                            *
 ******************************************************************************/
static void	trx_agent_values_clean(trx_agent_value_t *values, size_t values_num)
{
	size_t	i;

	for (i = 0; i < values_num; i++)
	{
		trx_free(values[i].value);
		trx_free(values[i].source);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: log_client_timediff                                              *
 *                                                                            *
 * Purpose: calculates difference between server and client (proxy, active    *
 *          agent or sender) time and log it                                  *
 *                                                                            *
 * Parameters: level   - [IN] log level                                       *
 *             jp      - [IN] JSON with clock, [ns] fields                    *
 *             ts_recv - [IN] the connection timestamp                        *
 *                                                                            *
 ******************************************************************************/
static void	log_client_timediff(int level, struct trx_json_parse *jp, const trx_timespec_t *ts_recv)
{
	char		tmp[32];
	trx_timespec_t	client_timediff;
	int		sec, ns;

	if (SUCCEED != TRX_CHECK_LOG_LEVEL(level))
		return;

	if (SUCCEED == trx_json_value_by_name(jp, TRX_PROTO_TAG_CLOCK, tmp, sizeof(tmp)))
	{
		sec = atoi(tmp);
		client_timediff.sec = ts_recv->sec - sec;

		if (SUCCEED == trx_json_value_by_name(jp, TRX_PROTO_TAG_NS, tmp, sizeof(tmp)))
		{
			ns = atoi(tmp);
			client_timediff.ns = ts_recv->ns - ns;

			if (client_timediff.sec > 0 && client_timediff.ns < 0)
			{
				client_timediff.sec--;
				client_timediff.ns += 1000000000;
			}
			else if (client_timediff.sec < 0 && client_timediff.ns > 0)
			{
				client_timediff.sec++;
				client_timediff.ns -= 1000000000;
			}

			treegix_log(level, "%s(): timestamp from json %d seconds and %d nanosecond, "
					"delta time from json %d seconds and %d nanosecond",
					__func__, sec, ns, client_timediff.sec, client_timediff.ns);
		}
		else
		{
			treegix_log(level, "%s(): timestamp from json %d seconds, "
				"delta time from json %d seconds", __func__, sec, client_timediff.sec);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: parse_history_data_row_value                                     *
 *                                                                            *
 * Purpose: parses agent value from history data json row                     *
 *                                                                            *
 * Parameters: jp_row       - [IN] JSON with history data row                 *
 *             unique_shift - [IN/OUT] auto increment nanoseconds to ensure   *
 *                                     unique value of timestamps             *
 *             av           - [OUT] the agent value                           *
 *                                                                            *
 * Return value:  SUCCEED - the value was parsed successfully                 *
 *                FAIL    - otherwise                                         *
 *                                                                            *
 ******************************************************************************/
static int	parse_history_data_row_value(const struct trx_json_parse *jp_row, trx_timespec_t *unique_shift,
		trx_agent_value_t *av)
{
	char	*tmp = NULL;
	size_t	tmp_alloc = 0;
	int	ret = FAIL;

	memset(av, 0, sizeof(trx_agent_value_t));

	if (SUCCEED == trx_json_value_by_name_dyn(jp_row, TRX_PROTO_TAG_CLOCK, &tmp, &tmp_alloc))
	{
		if (FAIL == is_uint31(tmp, &av->ts.sec))
			goto out;

		if (SUCCEED == trx_json_value_by_name_dyn(jp_row, TRX_PROTO_TAG_NS, &tmp, &tmp_alloc))
		{
			if (FAIL == is_uint_n_range(tmp, tmp_alloc, &av->ts.ns, sizeof(av->ts.ns),
				0LL, 999999999LL))
			{
				goto out;
			}
		}
		else
		{
			/* ensure unique value timestamp (clock, ns) if only clock is available */

			av->ts.sec += unique_shift->sec;
			av->ts.ns = unique_shift->ns++;

			if (unique_shift->ns > 999999999)
			{
				unique_shift->sec++;
				unique_shift->ns = 0;
			}
		}
	}
	else
		trx_timespec(&av->ts);

	if (SUCCEED == trx_json_value_by_name_dyn(jp_row, TRX_PROTO_TAG_STATE, &tmp, &tmp_alloc))
		av->state = (unsigned char)atoi(tmp);

	/* Unsupported item meta information must be ignored for backwards compatibility. */
	/* New agents will not send meta information for items in unsupported state.      */
	if (ITEM_STATE_NOTSUPPORTED != av->state)
	{
		if (SUCCEED == trx_json_value_by_name_dyn(jp_row, TRX_PROTO_TAG_LASTLOGSIZE, &tmp, &tmp_alloc))
		{
			av->meta = 1;	/* contains meta information */

			is_uint64(tmp, &av->lastlogsize);

			if (SUCCEED == trx_json_value_by_name_dyn(jp_row, TRX_PROTO_TAG_MTIME, &tmp, &tmp_alloc))
				av->mtime = atoi(tmp);
		}
	}

	if (SUCCEED == trx_json_value_by_name_dyn(jp_row, TRX_PROTO_TAG_VALUE, &tmp, &tmp_alloc))
		av->value = trx_strdup(av->value, tmp);

	if (SUCCEED == trx_json_value_by_name_dyn(jp_row, TRX_PROTO_TAG_LOGTIMESTAMP, &tmp, &tmp_alloc))
		av->timestamp = atoi(tmp);

	if (SUCCEED == trx_json_value_by_name_dyn(jp_row, TRX_PROTO_TAG_LOGSOURCE, &tmp, &tmp_alloc))
		av->source = trx_strdup(av->source, tmp);

	if (SUCCEED == trx_json_value_by_name_dyn(jp_row, TRX_PROTO_TAG_LOGSEVERITY, &tmp, &tmp_alloc))
		av->severity = atoi(tmp);

	if (SUCCEED == trx_json_value_by_name_dyn(jp_row, TRX_PROTO_TAG_LOGEVENTID, &tmp, &tmp_alloc))
		av->logeventid = atoi(tmp);

	if (SUCCEED != trx_json_value_by_name_dyn(jp_row, TRX_PROTO_TAG_ID, &tmp, &tmp_alloc) ||
			SUCCEED != is_uint64(tmp, &av->id))
	{
		av->id = 0;
	}

	trx_free(tmp);

	ret = SUCCEED;
out:
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: parse_history_data_row_itemid                                    *
 *                                                                            *
 * Purpose: parses item identifier from history data json row                 *
 *                                                                            *
 * Parameters: jp_row - [IN] JSON with history data row                       *
 *             itemid - [OUT] the item identifier                             *
 *                                                                            *
 * Return value:  SUCCEED - the item identifier was parsed successfully       *
 *                FAIL    - otherwise                                         *
 *                                                                            *
 ******************************************************************************/
static int	parse_history_data_row_itemid(const struct trx_json_parse *jp_row, trx_uint64_t *itemid)
{
	char	buffer[MAX_ID_LEN + 1];

	if (SUCCEED != trx_json_value_by_name(jp_row, TRX_PROTO_TAG_ITEMID, buffer, sizeof(buffer)))
		return FAIL;

	if (SUCCEED != is_uint64(buffer, itemid))
		return FAIL;

	return SUCCEED;
}
/******************************************************************************
 *                                                                            *
 * Function: parse_history_data_row_hostkey                                   *
 *                                                                            *
 * Purpose: parses host,key pair from history data json row                   *
 *                                                                            *
 * Parameters: jp_row - [IN] JSON with history data row                       *
 *             hk     - [OUT] the host,key pair                               *
 *                                                                            *
 * Return value:  SUCCEED - the host,key pair was parsed successfully         *
 *                FAIL    - otherwise                                         *
 *                                                                            *
 ******************************************************************************/
static int	parse_history_data_row_hostkey(const struct trx_json_parse *jp_row, trx_host_key_t *hk)
{
	char	buffer[MAX_STRING_LEN];

	if (SUCCEED != trx_json_value_by_name(jp_row, TRX_PROTO_TAG_HOST, buffer, sizeof(buffer)))
		return FAIL;

	hk->host = trx_strdup(hk->host, buffer);

	if (SUCCEED != trx_json_value_by_name(jp_row, TRX_PROTO_TAG_KEY, buffer, sizeof(buffer)))
	{
		trx_free(hk->host);
		return FAIL;
	}

	hk->key = trx_strdup(hk->key, buffer);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: parse_history_data                                               *
 *                                                                            *
 * Purpose: parses up to TRX_HISTORY_VALUES_MAX item values and host,key      *
 *          pairs from history data json                                      *
 *                                                                            *
 * Parameters: jp_data      - [IN] JSON with history data array               *
 *             pnext        - [IN/OUT] the pointer to the next item in json,  *
 *                                     NULL - no more data left               *
 *             values       - [OUT] the item values                           *
 *             hostkeys     - [OUT] the corresponding host,key pairs          *
 *             values_num   - [OUT] number of elements in values and hostkeys *
 *                                  arrays                                    *
 *             parsed_num   - [OUT] the number of values parsed               *
 *             unique_shift - [IN/OUT] auto increment nanoseconds to ensure   *
 *                                     unique value of timestamps             *
 *             info         - [OUT] address of a pointer to the info          *
 *                                  string (should be freed by the caller)    *
 *                                                                            *
 * Return value:  SUCCEED - values were parsed successfully                   *
 *                FAIL    - an error occurred                                 *
 *                                                                            *
 ******************************************************************************/
static int	parse_history_data(struct trx_json_parse *jp_data, const char **pnext, trx_agent_value_t *values,
		trx_host_key_t *hostkeys, int *values_num, int *parsed_num, trx_timespec_t *unique_shift,
		char **error)
{
	struct trx_json_parse	jp_row;
	int			ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	*values_num = 0;
	*parsed_num = 0;

	if (NULL == *pnext)
	{
		if (NULL == (*pnext = trx_json_next(jp_data, *pnext)) && *values_num < TRX_HISTORY_VALUES_MAX)
		{
			ret = SUCCEED;
			goto out;
		}
	}

	/* iterate the history data rows */
	do
	{
		if (FAIL == trx_json_brackets_open(*pnext, &jp_row))
		{
			*error = trx_strdup(*error, trx_json_strerror());
			goto out;
		}

		(*parsed_num)++;

		if (SUCCEED != parse_history_data_row_hostkey(&jp_row, &hostkeys[*values_num]))
			continue;

		if (SUCCEED != parse_history_data_row_value(&jp_row, unique_shift, &values[*values_num]))
			continue;

		(*values_num)++;
	}
	while (NULL != (*pnext = trx_json_next(jp_data, *pnext)) && *values_num < TRX_HISTORY_VALUES_MAX);

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s processed:%d/%d", __func__, trx_result_string(ret),
			*values_num, *parsed_num);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: parse_history_data_by_itemids                                    *
 *                                                                            *
 * Purpose: parses up to TRX_HISTORY_VALUES_MAX item values and item          *
 *          identifiers from history data json                                *
 *                                                                            *
 * Parameters: jp_data      - [IN] JSON with history data array               *
 *             pnext        - [IN/OUT] the pointer to the next item in        *
 *                                        json, NULL - no more data left      *
 *             values       - [OUT] the item values                           *
 *             itemids      - [OUT] the corresponding item identifiers        *
 *             values_num   - [OUT] number of elements in values and itemids  *
 *                                  arrays                                    *
 *             parsed_num   - [OUT] the number of values parsed               *
 *             unique_shift - [IN/OUT] auto increment nanoseconds to ensure   *
 *                                     unique value of timestamps             *
 *             info         - [OUT] address of a pointer to the info string   *
 *                                  (should be freed by the caller)           *
 *                                                                            *
 * Return value:  SUCCEED - values were parsed successfully                   *
 *                FAIL    - an error occurred                                 *
 *                                                                            *
 * Comments: This function is used to parse the new proxy history data        *
 *           protocol introduced in Treegix v3.3.                              *
 *                                                                            *
 ******************************************************************************/
static int	parse_history_data_by_itemids(struct trx_json_parse *jp_data, const char **pnext,
		trx_agent_value_t *values, trx_uint64_t *itemids, int *values_num, int *parsed_num,
		trx_timespec_t *unique_shift, char **error)
{
	struct trx_json_parse	jp_row;
	int			ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	*values_num = 0;
	*parsed_num = 0;

	if (NULL == *pnext)
	{
		if (NULL == (*pnext = trx_json_next(jp_data, *pnext)) && *values_num < TRX_HISTORY_VALUES_MAX)
		{
			ret = SUCCEED;
			goto out;
		}
	}

	/* iterate the history data rows */
	do
	{
		if (FAIL == trx_json_brackets_open(*pnext, &jp_row))
		{
			*error = trx_strdup(*error, trx_json_strerror());
			goto out;
		}

		(*parsed_num)++;

		if (SUCCEED != parse_history_data_row_itemid(&jp_row, &itemids[*values_num]))
			continue;

		if (SUCCEED != parse_history_data_row_value(&jp_row, unique_shift, &values[*values_num]))
			continue;

		(*values_num)++;
	}
	while (NULL != (*pnext = trx_json_next(jp_data, *pnext)) && *values_num < TRX_HISTORY_VALUES_MAX);

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s processed:%d/%d", __func__, trx_result_string(ret),
			*values_num, *parsed_num);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: proxy_item_validator                                             *
 *                                                                            *
 * Purpose: validates item received from proxy                                *
 *                                                                            *
 * Parameters: item  - [IN/OUT] the item data                                 *
 *             sock  - [IN] the connection socket                             *
 *             args  - [IN] the validator arguments                           *
 *             error - unused                                                 *
 *                                                                            *
 * Return value:  SUCCEED - the validation was successful                     *
 *                FAIL    - otherwise                                         *
 *                                                                            *
 ******************************************************************************/
static int	proxy_item_validator(DC_ITEM *item, trx_socket_t *sock, void *args, char **error)
{
	trx_uint64_t	*proxyid = (trx_uint64_t *)args;

	TRX_UNUSED(sock);
	TRX_UNUSED(error);

	/* don't process item if its host was assigned to another proxy */
	if (item->host.proxy_hostid != *proxyid)
		return FAIL;

	/* don't process aggregate/calculated items coming from proxy */
	if (ITEM_TYPE_AGGREGATE == item->type || ITEM_TYPE_CALCULATED == item->type)
		return FAIL;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: process_history_data_by_itemids                                  *
 *                                                                            *
 * Purpose: parses history data array and process the data                    *
 *                                                                            *
 * Parameters: proxy        - [IN] the proxy                                  *
 *             jp_data      - [IN] JSON with history data array               *
 *             session      - [IN] the data session                           *
 *             unique_shift - [IN/OUT] auto increment nanoseconds to ensure   *
 *                                     unique value of timestamps             *
 *             info         - [OUT] address of a pointer to the info          *
 *                                     string (should be freed by the caller) *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 * Comments: This function is used to parse the new proxy history data        *
 *           protocol introduced in Treegix v3.3.                              *
 *                                                                            *
 ******************************************************************************/
static int	process_history_data_by_itemids(trx_socket_t *sock, trx_client_item_validator_t validator_func,
		void *validator_args, struct trx_json_parse *jp_data, trx_data_session_t *session, char **info)
{
	const char		*pnext = NULL;
	int			ret = SUCCEED, processed_num = 0, total_num = 0, values_num, read_num, i, *errcodes;
	double			sec;
	DC_ITEM			*items;
	char			*error = NULL;
	trx_uint64_t		itemids[TRX_HISTORY_VALUES_MAX];
	trx_agent_value_t	values[TRX_HISTORY_VALUES_MAX];
	trx_timespec_t		unique_shift = {0, 0};

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	items = (DC_ITEM *)trx_malloc(NULL, sizeof(DC_ITEM) * TRX_HISTORY_VALUES_MAX);
	errcodes = (int *)trx_malloc(NULL, sizeof(int) * TRX_HISTORY_VALUES_MAX);

	sec = trx_time();

	while (SUCCEED == parse_history_data_by_itemids(jp_data, &pnext, values, itemids, &values_num, &read_num,
			&unique_shift, &error) && 0 != values_num)
	{
		DCconfig_get_items_by_itemids(items, itemids, errcodes, values_num);

		for (i = 0; i < values_num; i++)
		{
			if (SUCCEED != errcodes[i])
				continue;

			/* check and discard if duplicate data */
			if (NULL != session && 0 != values[i].id && values[i].id <= session->last_valueid)
			{
				DCconfig_clean_items(&items[i], &errcodes[i], 1);
				errcodes[i] = FAIL;
				continue;
			}

			if (SUCCEED != validator_func(&items[i], sock, validator_args, &error))
			{
				if (NULL != error)
				{
					treegix_log(LOG_LEVEL_WARNING, "%s", error);
					trx_free(error);
				}

				DCconfig_clean_items(&items[i], &errcodes[i], 1);
				errcodes[i] = FAIL;
			}
		}

		processed_num += process_history_data(items, values, errcodes, values_num);

		total_num += read_num;

		if (NULL != session)
			session->last_valueid = values[values_num - 1].id;

		DCconfig_clean_items(items, errcodes, values_num);
		trx_agent_values_clean(values, values_num);

		if (NULL == pnext)
			break;
	}

	trx_free(errcodes);
	trx_free(items);

	if (NULL == error)
	{
		ret = SUCCEED;
		*info = trx_dsprintf(*info, "processed: %d; failed: %d; total: %d; seconds spent: " TRX_FS_DBL,
				processed_num, total_num - processed_num, total_num, trx_time() - sec);
	}
	else
	{
		trx_free(*info);
		*info = error;
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: agent_item_validator                                             *
 *                                                                            *
 * Purpose: validates item received from active agent                         *
 *                                                                            *
 * Parameters: item  - [IN] the item data                                     *
 *             sock  - [IN] the connection socket                             *
 *             args  - [IN] the validator arguments                           *
 *             error - [OUT] the error message                                *
 *                                                                            *
 * Return value:  SUCCEED - the validation was successful                     *
 *                FAIL    - otherwise                                         *
 *                                                                            *
 ******************************************************************************/
static int	agent_item_validator(DC_ITEM *item, trx_socket_t *sock, void *args, char **error)
{
	trx_host_rights_t	*rights = (trx_host_rights_t *)args;

	if (0 != item->host.proxy_hostid)
		return FAIL;

	if (ITEM_TYPE_TREEGIX_ACTIVE != item->type)
		return FAIL;

	if (rights->hostid != item->host.hostid)
	{
		rights->hostid = item->host.hostid;
		rights->value = trx_host_check_permissions(&item->host, sock, error);
	}

	return rights->value;
}

/******************************************************************************
 *                                                                            *
 * Function: sender_item_validator                                            *
 *                                                                            *
 * Purpose: validates item received from sender                               *
 *                                                                            *
 * Parameters: item  - [IN] the item data                                     *
 *             sock  - [IN] the connection socket                             *
 *             args  - [IN] the validator arguments                           *
 *             error - [OUT] the error message                                *
 *                                                                            *
 * Return value:  SUCCEED - the validation was successful                     *
 *                FAIL    - otherwise                                         *
 *                                                                            *
 ******************************************************************************/
static int	sender_item_validator(DC_ITEM *item, trx_socket_t *sock, void *args, char **error)
{
	trx_host_rights_t	*rights;

	if (0 != item->host.proxy_hostid)
		return FAIL;

	switch(item->type)
	{
		case ITEM_TYPE_HTTPAGENT:
			if (0 == item->allow_traps)
			{
				*error = trx_dsprintf(*error, "cannot process HTTP agent item \"%s\" trap:"
						" trapping is not enabled", item->key_orig);
				return FAIL;
			}
			break;
		case ITEM_TYPE_TRAPPER:
			break;
		default:
			*error = trx_dsprintf(*error, "cannot process item \"%s\" trap:"
					" item type \"%d\" cannot be used with traps", item->key_orig, item->type);
			return FAIL;
	}

	if ('\0' != *item->trapper_hosts)	/* list of allowed hosts not empty */
	{
		char	*allowed_peers;
		int	ret;

		allowed_peers = trx_strdup(NULL, item->trapper_hosts);
		substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, item, NULL, NULL,
				&allowed_peers, MACRO_TYPE_ALLOWED_HOSTS, NULL, 0);
		ret = trx_tcp_check_allowed_peers(sock, allowed_peers);
		trx_free(allowed_peers);

		if (FAIL == ret)
		{
			*error = trx_dsprintf(*error, "cannot process item \"%s\" trap: %s",
					item->key_orig, trx_socket_strerror());
			return FAIL;
		}
	}

	rights = (trx_host_rights_t *)args;

	if (rights->hostid != item->host.hostid)
	{
		rights->hostid = item->host.hostid;
		rights->value = trx_host_check_permissions(&item->host, sock, error);
	}

	return rights->value;
}

static int	process_history_data_by_keys(trx_socket_t *sock, trx_client_item_validator_t validator_func,
		void *validator_args, char **info, struct trx_json_parse *jp_data, const char *token)
{
	int			ret, values_num, read_num, processed_num = 0, total_num = 0, i;
	trx_timespec_t		unique_shift = {0, 0};
	const char		*pnext = NULL;
	char			*error = NULL;
	trx_host_key_t		*hostkeys;
	DC_ITEM			*items;
	trx_data_session_t	*session = NULL;
	trx_uint64_t		last_hostid = 0;
	trx_agent_value_t	values[TRX_HISTORY_VALUES_MAX];
	int			errcodes[TRX_HISTORY_VALUES_MAX];
	double			sec;

	sec = trx_time();

	items = (DC_ITEM *)trx_malloc(NULL, sizeof(DC_ITEM) * TRX_HISTORY_VALUES_MAX);
	hostkeys = (trx_host_key_t *)trx_malloc(NULL, sizeof(trx_host_key_t) * TRX_HISTORY_VALUES_MAX);
	memset(hostkeys, 0, sizeof(trx_host_key_t) * TRX_HISTORY_VALUES_MAX);

	while (SUCCEED == parse_history_data(jp_data, &pnext, values, hostkeys, &values_num, &read_num,
			&unique_shift, &error) && 0 != values_num)
	{
		DCconfig_get_items_by_keys(items, hostkeys, errcodes, values_num);

		for (i = 0; i < values_num; i++)
		{
			if (SUCCEED != errcodes[i])
				continue;

			if (last_hostid != items[i].host.hostid)
			{
				last_hostid = items[i].host.hostid;

				if (NULL != token)
					session = trx_dc_get_or_create_data_session(last_hostid, token);
			}

			/* check and discard if duplicate data */
			if (NULL != session && 0 != values[i].id && values[i].id <= session->last_valueid)
			{
				DCconfig_clean_items(&items[i], &errcodes[i], 1);
				errcodes[i] = FAIL;
				continue;
			}

			if (SUCCEED != validator_func(&items[i], sock, validator_args, &error))
			{
				if (NULL != error)
				{
					treegix_log(LOG_LEVEL_WARNING, "%s", error);
					trx_free(error);
				}

				DCconfig_clean_items(&items[i], &errcodes[i], 1);
				errcodes[i] = FAIL;
			}

			if (NULL != session)
				session->last_valueid = values[i].id;
		}

		processed_num += process_history_data(items, values, errcodes, values_num);
		total_num += read_num;

		DCconfig_clean_items(items, errcodes, values_num);
		trx_agent_values_clean(values, values_num);

		if (NULL == pnext)
			break;
	}

	for (i = 0; i < TRX_HISTORY_VALUES_MAX; i++)
	{
		trx_free(hostkeys[i].host);
		trx_free(hostkeys[i].key);
	}

	trx_free(hostkeys);
	trx_free(items);

	if (NULL == error)
	{
		ret = SUCCEED;
		*info = trx_dsprintf(*info, "processed: %d; failed: %d; total: %d; seconds spent: " TRX_FS_DBL,
				processed_num, total_num - processed_num, total_num, trx_time() - sec);
	}
	else
	{
		trx_free(*info);
		*info = error;
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_client_history_data                                      *
 *                                                                            *
 * Purpose: process history data sent by proxy/agent/sender                   *
 *                                                                            *
 * Parameters: sock           - [IN] the connection socket                    *
 *             jp             - [IN] JSON with historical data                *
 *             ts             - [IN] the client connection timestamp          *
 *             validator_func - [IN] the item validator callback function     *
 *             validator_args - [IN] the user arguments passed to validator   *
 *                                   function                                 *
 *             info           - [OUT] address of a pointer to the info string *
 *                                    (should be freed by the caller)         *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
static int	process_client_history_data(trx_socket_t *sock, struct trx_json_parse *jp, trx_timespec_t *ts,
		trx_client_item_validator_t validator_func, void *validator_args, char **info)
{
	int			ret;
	char			*token = NULL;
	size_t			token_alloc = 0;
	struct trx_json_parse	jp_data;
	char			tmp[MAX_STRING_LEN];
	int			version;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	log_client_timediff(LOG_LEVEL_DEBUG, jp, ts);

	if (SUCCEED != (ret = trx_json_brackets_by_name(jp, TRX_PROTO_TAG_DATA, &jp_data)))
	{
		*info = trx_strdup(*info, trx_json_strerror());
		goto out;
	}

	if (SUCCEED == trx_json_value_by_name_dyn(jp, TRX_PROTO_TAG_SESSION, &token, &token_alloc))
	{
		size_t	token_len;

		if (TRX_DATA_SESSION_TOKEN_SIZE != (token_len = strlen(token)))
		{
			*info = trx_dsprintf(*info, "invalid session token length %d", (int)token_len);
			ret = FAIL;
			goto out;
		}
	}

	if (SUCCEED != trx_json_value_by_name(jp, TRX_PROTO_TAG_VERSION, tmp, sizeof(tmp)) ||
				FAIL == (version = trx_get_component_version(tmp)))
	{
		version = TRX_COMPONENT_VERSION(4, 2);
	}

	if (TRX_COMPONENT_VERSION(4, 4) <= version &&
			SUCCEED == trx_json_value_by_name(jp, TRX_PROTO_TAG_HOST, tmp, sizeof(tmp)))
	{
		trx_data_session_t	*session;
		trx_uint64_t		hostid;

		if (SUCCEED != DCconfig_get_hostid_by_name(tmp, &hostid))
		{
			*info = trx_dsprintf(*info, "unknown host '%s'", tmp);
			ret = SUCCEED;
			goto out;
		}

		if (NULL == token)
			session = NULL;
		else
			session = trx_dc_get_or_create_data_session(hostid, token);

		if (SUCCEED != (ret = process_history_data_by_itemids(sock, validator_func, validator_args, &jp_data,
				session, info)))
		{
			goto out;
		}
	}
	else
		ret = process_history_data_by_keys(sock, validator_func, validator_args, info, &jp_data, token);
out:
	trx_free(token);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_agent_history_data                                       *
 *                                                                            *
 * Purpose: process history data received from Treegix active agent            *
 *                                                                            *
 * Parameters: sock         - [IN] the connection socket                      *
 *             jp           - [IN] the JSON with history data                 *
 *             ts           - [IN] the connection timestamp                   *
 *             info         - [OUT] address of a pointer to the info string   *
 *                                  (should be freed by the caller)           *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
int	process_agent_history_data(trx_socket_t *sock, struct trx_json_parse *jp, trx_timespec_t *ts, char **info)
{
	trx_host_rights_t	rights = {0};

	return process_client_history_data(sock, jp, ts, agent_item_validator, &rights, info);
}

/******************************************************************************
 *                                                                            *
 * Function: process_sender_history_data                                      *
 *                                                                            *
 * Purpose: process history data received from Treegix sender                  *
 *                                                                            *
 * Parameters: sock         - [IN] the connection socket                      *
 *             jp           - [IN] the JSON with history data                 *
 *             ts           - [IN] the connection timestamp                   *
 *             info         - [OUT] address of a pointer to the info string   *
 *                                  (should be freed by the caller)           *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
int	process_sender_history_data(trx_socket_t *sock, struct trx_json_parse *jp, trx_timespec_t *ts, char **info)
{
	trx_host_rights_t	rights = {0};

	return process_client_history_data(sock, jp, ts, sender_item_validator, &rights, info);
}

static void	trx_drule_ip_free(trx_drule_ip_t *ip)
{
	trx_vector_ptr_clear_ext(&ip->services, trx_ptr_free);
	trx_vector_ptr_destroy(&ip->services);
	trx_free(ip);
}

static void	trx_drule_free(trx_drule_t *drule)
{
	trx_vector_ptr_clear_ext(&drule->ips, (trx_clean_func_t)trx_drule_ip_free);
	trx_vector_ptr_destroy(&drule->ips);
	trx_free(drule);
}

/******************************************************************************
 *                                                                            *
 * Function: process_services                                                 *
 *                                                                            *
 * Purpose: process services discovered on IP address                         *
 *                                                                            *
 * Parameters: drule_ptr         - [IN] discovery rule structure              *
 *             ip_discovered_ptr - [IN] vector of ip addresses                *
 *                                                                            *
 ******************************************************************************/
static int	process_services(const trx_vector_ptr_t *services, const char *ip, trx_uint64_t druleid,
		trx_uint64_t unique_dcheckid, int *processed_num, int ip_idx)
{
	DB_DHOST		dhost;
	trx_service_t		*service;
	int			services_num, ret = FAIL, i;
	trx_vector_uint64_t	dcheckids;
	trx_vector_ptr_t	services_old;
	DB_DRULE		drule = {.druleid = druleid, .unique_dcheckid = unique_dcheckid};

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	memset(&dhost, 0, sizeof(dhost));

	trx_vector_uint64_create(&dcheckids);
	trx_vector_ptr_create(&services_old);

	/* find host update */
	for (i = *processed_num; i < services->values_num; i++)
	{
		service = (trx_service_t *)services->values[i];

		treegix_log(LOG_LEVEL_DEBUG, "%s() druleid:" TRX_FS_UI64 " dcheckid:" TRX_FS_UI64 " unique_dcheckid:"
				TRX_FS_UI64 " time:'%s %s' ip:'%s' dns:'%s' port:%hu status:%d value:'%s'",
				__func__, drule.druleid, service->dcheckid, drule.unique_dcheckid,
				trx_date2str(service->itemtime), trx_time2str(service->itemtime), ip, service->dns,
				service->port, service->status, service->value);

		if (0 == service->dcheckid)
			break;

		trx_vector_uint64_append(&dcheckids, service->dcheckid);
	}

	/* stop processing current discovery rule and save proxy history until host update is available */
	if (i == services->values_num)
	{
		DBbegin();

		for (i = *processed_num; i < services->values_num; i++)
		{
			char	*ip_esc, *dns_esc, *value_esc;

			service = (trx_service_t *)services->values[i];

			ip_esc = DBdyn_escape_field("proxy_dhistory", "ip", ip);
			dns_esc = DBdyn_escape_field("proxy_dhistory", "dns", service->dns);
			value_esc = DBdyn_escape_field("proxy_dhistory", "value", service->value);

			DBexecute("insert into proxy_dhistory (clock,druleid,ip,port,value,status,dcheckid,dns)"
					" values (%d," TRX_FS_UI64 ",'%s',%d,'%s',%d," TRX_FS_UI64 ",'%s')",
					(int)service->itemtime, drule.druleid, ip_esc, service->port,
					value_esc, service->status, service->dcheckid, dns_esc);
			trx_free(value_esc);
			trx_free(dns_esc);
			trx_free(ip_esc);
		}

		DBcommit();

		goto fail;
	}

	services_num = i;

	if (0 == *processed_num && 0 == ip_idx)
	{
		DB_RESULT	result;
		DB_ROW		row;
		trx_uint64_t	dcheckid;

		result = DBselect(
				"select dcheckid,clock,port,value,status,dns,ip"
				" from proxy_dhistory"
				" where druleid=" TRX_FS_UI64
				" order by id",
				drule.druleid);

		for (i = 0; NULL != (row = DBfetch(result)); i++)
		{
			if (SUCCEED == DBis_null(row[0]))
				continue;

			TRX_STR2UINT64(dcheckid, row[0]);

			if (0 == strcmp(ip, row[6]))
			{
				service = (trx_service_t *)trx_malloc(NULL, sizeof(trx_service_t));
				service->dcheckid = dcheckid;
				service->itemtime = (time_t)atoi(row[1]);
				service->port = atoi(row[2]);
				trx_strlcpy_utf8(service->value, row[3], MAX_DISCOVERED_VALUE_SIZE);
				service->status = atoi(row[4]);
				trx_strlcpy(service->dns, row[5], INTERFACE_DNS_LEN_MAX);
				trx_vector_ptr_append(&services_old, service);
				trx_vector_uint64_append(&dcheckids, service->dcheckid);
			}
		}
		DBfree_result(result);

		if (0 != i)
		{
			DBexecute("delete from proxy_dhistory"
					" where druleid=" TRX_FS_UI64,
					drule.druleid);
		}
	}

	if (0 == dcheckids.values_num)
	{
		treegix_log(LOG_LEVEL_DEBUG, "cannot process host update without services");
		goto fail;
	}

	DBbegin();

	if (SUCCEED != DBlock_druleid(drule.druleid))
	{
		DBrollback();
		treegix_log(LOG_LEVEL_DEBUG, "druleid:" TRX_FS_UI64 " does not exist", drule.druleid);
		goto fail;
	}

	trx_vector_uint64_sort(&dcheckids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	trx_vector_uint64_uniq(&dcheckids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	if (SUCCEED != DBlock_ids("dchecks", "dcheckid", &dcheckids))
	{
		DBrollback();
		treegix_log(LOG_LEVEL_DEBUG, "checks are not available for druleid:" TRX_FS_UI64, drule.druleid);
		goto fail;
	}

	for (i = 0; i < services_old.values_num; i++)
	{
		service = (trx_service_t *)services_old.values[i];

		if (FAIL == trx_vector_uint64_bsearch(&dcheckids, service->dcheckid, TRX_DEFAULT_UINT64_COMPARE_FUNC))
		{
			treegix_log(LOG_LEVEL_DEBUG, "dcheckid:" TRX_FS_UI64 " does not exist", service->dcheckid);
			continue;
		}

		discovery_update_service(&drule, service->dcheckid, &dhost, ip, service->dns, service->port,
				service->status, service->value, service->itemtime);
	}

	for (;*processed_num < services_num; (*processed_num)++)
	{
		service = (trx_service_t *)services->values[*processed_num];

		if (FAIL == trx_vector_uint64_bsearch(&dcheckids, service->dcheckid, TRX_DEFAULT_UINT64_COMPARE_FUNC))
		{
			treegix_log(LOG_LEVEL_DEBUG, "dcheckid:" TRX_FS_UI64 " does not exist", service->dcheckid);
			continue;
		}

		discovery_update_service(&drule, service->dcheckid, &dhost, ip, service->dns, service->port,
				service->status, service->value, service->itemtime);
	}

	service = (trx_service_t *)services->values[(*processed_num)++];
	discovery_update_host(&dhost, service->status, service->itemtime);

	DBcommit();
	ret = SUCCEED;
fail:
	trx_vector_ptr_clear_ext(&services_old, trx_ptr_free);
	trx_vector_ptr_destroy(&services_old);
	trx_vector_uint64_destroy(&dcheckids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_discovery_data_contents                                  *
 *                                                                            *
 * Purpose: parse discovery data contents and process it                      *
 *                                                                            *
 * Parameters: jp_data         - [IN] JSON with discovery data                *
 *             error           - [OUT] address of a pointer to the info       *
 *                                     string (should be freed by the caller) *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
static int	process_discovery_data_contents(struct trx_json_parse *jp_data, char **error)
{
	DB_RESULT		result;
	DB_ROW			row;
	trx_uint64_t		dcheckid, druleid;
	struct trx_json_parse	jp_row;
	int			status, ret = SUCCEED, i, j;
	unsigned short		port;
	const char		*p = NULL;
	char			ip[INTERFACE_IP_LEN_MAX],
				tmp[MAX_STRING_LEN], *value = NULL, dns[INTERFACE_DNS_LEN_MAX];
	time_t			itemtime;
	size_t			value_alloc = MAX_DISCOVERED_VALUE_SIZE;
	trx_vector_ptr_t	drules;
	trx_drule_t		*drule;
	trx_drule_ip_t		*drule_ip;
	trx_service_t		*service;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	value = (char *)trx_malloc(value, value_alloc);

	trx_vector_ptr_create(&drules);

	while (NULL != (p = trx_json_next(jp_data, p)))
	{
		if (FAIL == trx_json_brackets_open(p, &jp_row))
			goto json_parse_error;

		if (FAIL == trx_json_value_by_name(&jp_row, TRX_PROTO_TAG_CLOCK, tmp, sizeof(tmp)))
			goto json_parse_error;

		itemtime = atoi(tmp);

		if (FAIL == trx_json_value_by_name(&jp_row, TRX_PROTO_TAG_DRULE, tmp, sizeof(tmp)))
			goto json_parse_error;

		TRX_STR2UINT64(druleid, tmp);

		if (FAIL == trx_json_value_by_name(&jp_row, TRX_PROTO_TAG_DCHECK, tmp, sizeof(tmp)))
			goto json_parse_error;

		if ('\0' != *tmp)
			TRX_STR2UINT64(dcheckid, tmp);
		else
			dcheckid = 0;

		if (FAIL == trx_json_value_by_name(&jp_row, TRX_PROTO_TAG_IP, ip, sizeof(ip)))
			goto json_parse_error;

		if (SUCCEED != is_ip(ip))
		{
			treegix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid IP address", __func__, ip);
			continue;
		}

		if (FAIL == trx_json_value_by_name(&jp_row, TRX_PROTO_TAG_PORT, tmp, sizeof(tmp)))
		{
			port = 0;
		}
		else if (FAIL == is_ushort(tmp, &port))
		{
			treegix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid port", __func__, tmp);
			continue;
		}

		if (SUCCEED != trx_json_value_by_name_dyn(&jp_row, TRX_PROTO_TAG_VALUE, &value, &value_alloc))
			*value = '\0';

		if (FAIL == trx_json_value_by_name(&jp_row, TRX_PROTO_TAG_DNS, dns, sizeof(dns)))
		{
			*dns = '\0';
		}
		else if ('\0' != *dns && FAIL == trx_validate_hostname(dns))
		{
			treegix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid hostname", __func__, dns);
			continue;
		}

		if (SUCCEED == trx_json_value_by_name(&jp_row, TRX_PROTO_TAG_STATUS, tmp, sizeof(tmp)))
			status = atoi(tmp);
		else
			status = 0;

		if (FAIL == (i = trx_vector_ptr_search(&drules, &druleid, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			drule = (trx_drule_t *)trx_malloc(NULL, sizeof(trx_drule_t));
			drule->druleid = druleid;
			trx_vector_ptr_create(&drule->ips);
			trx_vector_ptr_append(&drules, drule);
		}
		else
			drule = drules.values[i];

		if (FAIL == (i = trx_vector_ptr_search(&drule->ips, ip, TRX_DEFAULT_STR_COMPARE_FUNC)))
		{
			drule_ip = (trx_drule_ip_t *)trx_malloc(NULL, sizeof(trx_drule_ip_t));
			trx_strlcpy(drule_ip->ip, ip, INTERFACE_IP_LEN_MAX);
			trx_vector_ptr_create(&drule_ip->services);
			trx_vector_ptr_append(&drule->ips, drule_ip);
		}
		else
			drule_ip = drule->ips.values[i];

		service = (trx_service_t *)trx_malloc(NULL, sizeof(trx_service_t));
		service->dcheckid = dcheckid;
		service->port = port;
		service->status = status;
		trx_strlcpy_utf8(service->value, value, MAX_DISCOVERED_VALUE_SIZE);
		trx_strlcpy(service->dns, dns, INTERFACE_DNS_LEN_MAX);
		service->itemtime = itemtime;
		trx_vector_ptr_append(&drule_ip->services, service);

		continue;
json_parse_error:
		*error = trx_strdup(*error, trx_json_strerror());
		ret = FAIL;
		goto json_parse_return;
	}

	for (i = 0; i < drules.values_num; i++)
	{
		trx_uint64_t	unique_dcheckid;
		int		ret2 = SUCCEED;

		drule = (trx_drule_t *)drules.values[i];

		result = DBselect(
				"select dcheckid"
				" from dchecks"
				" where druleid=" TRX_FS_UI64
					" and uniq=1",
				drule->druleid);

		if (NULL != (row = DBfetch(result)))
			TRX_STR2UINT64(unique_dcheckid, row[0]);
		else
			unique_dcheckid = 0;
		DBfree_result(result);

		for (j = 0; j < drule->ips.values_num && SUCCEED == ret2; j++)
		{
			int	processed_num = 0;

			drule_ip = (trx_drule_ip_t *)drule->ips.values[j];

			while (processed_num != drule_ip->services.values_num)
			{
				if (FAIL == (ret2 = process_services(&drule_ip->services, drule_ip->ip, drule->druleid,
						unique_dcheckid, &processed_num, j)))
				{
					break;
				}
			}
		}
	}
json_parse_return:
	trx_free(value);

	trx_vector_ptr_clear_ext(&drules, (trx_clean_func_t)trx_drule_free);
	trx_vector_ptr_destroy(&drules);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_auto_registration_contents                               *
 *                                                                            *
 * Purpose: parse auto registration data contents and process it              *
 *                                                                            *
 * Parameters: jp_data         - [IN] JSON with auto registration data        *
 *             proxy_hostid    - [IN] proxy identifier from database          *
 *             error           - [OUT] address of a pointer to the info       *
 *                                     string (should be freed by the caller) *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
static int	process_auto_registration_contents(struct trx_json_parse *jp_data, trx_uint64_t proxy_hostid,
		char **error)
{
	struct trx_json_parse	jp_row;
	int			ret = SUCCEED;
	const char		*p = NULL;
	time_t			itemtime;
	char			host[HOST_HOST_LEN_MAX], ip[INTERFACE_IP_LEN_MAX], dns[INTERFACE_DNS_LEN_MAX],
				tmp[MAX_STRING_LEN], *host_metadata = NULL;
	unsigned short		port;
	size_t			host_metadata_alloc = 1;	/* for at least NUL-termination char */
	trx_vector_ptr_t	autoreg_hosts;
	trx_conn_flags_t	flags = TRX_CONN_DEFAULT;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&autoreg_hosts);
	host_metadata = (char *)trx_malloc(host_metadata, host_metadata_alloc);

	while (NULL != (p = trx_json_next(jp_data, p)))
	{
		unsigned int	connection_type;

		if (FAIL == (ret = trx_json_brackets_open(p, &jp_row)))
			break;

		if (FAIL == (ret = trx_json_value_by_name(&jp_row, TRX_PROTO_TAG_CLOCK, tmp, sizeof(tmp))))
			break;

		itemtime = atoi(tmp);

		if (FAIL == (ret = trx_json_value_by_name(&jp_row, TRX_PROTO_TAG_HOST, host, sizeof(host))))
			break;

		if (FAIL == trx_check_hostname(host, NULL))
		{
			treegix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid Treegix host name", __func__, host);
			continue;
		}

		if (FAIL == trx_json_value_by_name_dyn(&jp_row, TRX_PROTO_TAG_HOST_METADATA,
				&host_metadata, &host_metadata_alloc))
		{
			*host_metadata = '\0';
		}

		if (FAIL != trx_json_value_by_name(&jp_row, TRX_PROTO_TAG_FLAGS, tmp, sizeof(tmp)))
		{
			int flags_int;

			flags_int = atoi(tmp);

			switch (flags_int)
			{
				case TRX_CONN_DEFAULT:
				case TRX_CONN_IP:
				case TRX_CONN_DNS:
					flags = (trx_conn_flags_t)flags_int;
					break;
				default:
					flags = TRX_CONN_DEFAULT;
					treegix_log(LOG_LEVEL_WARNING, "wrong flags value: %d for host \"%s\":",
							flags_int, host);
			}
		}

		if (FAIL == (ret = trx_json_value_by_name(&jp_row, TRX_PROTO_TAG_IP, ip, sizeof(ip))))
		{
			if (TRX_CONN_DNS == flags)
			{
				*ip = '\0';
				ret = SUCCEED;
			}
			else
				break;
		}
		else if (SUCCEED != is_ip(ip))
		{
			treegix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid IP address", __func__, ip);
			continue;
		}

		if (FAIL == trx_json_value_by_name(&jp_row, TRX_PROTO_TAG_DNS, dns, sizeof(dns)))
		{
			*dns = '\0';
		}
		else if ('\0' != *dns && FAIL == trx_validate_hostname(dns))
		{
			treegix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid hostname", __func__, dns);
			continue;
		}

		if (FAIL == trx_json_value_by_name(&jp_row, TRX_PROTO_TAG_PORT, tmp, sizeof(tmp)))
		{
			port = TRX_DEFAULT_AGENT_PORT;
		}
		else if (FAIL == is_ushort(tmp, &port))
		{
			treegix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid port", __func__, tmp);
			continue;
		}
		else if (FAIL == trx_json_value_by_name(&jp_row, TRX_PROTO_TAG_TLS_ACCEPTED, tmp, sizeof(tmp)))
		{
			connection_type = TRX_TCP_SEC_UNENCRYPTED;
		}
		else if (FAIL == is_uint32(tmp, &connection_type) || (TRX_TCP_SEC_UNENCRYPTED != connection_type &&
				TRX_TCP_SEC_TLS_PSK != connection_type))
		{
			treegix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid value for \""
					TRX_PROTO_TAG_TLS_ACCEPTED "\"", __func__, tmp);
			continue;
		}

		DBregister_host_prepare(&autoreg_hosts, host, ip, dns, port, connection_type, host_metadata, flags,
				itemtime);
	}

	if (0 != autoreg_hosts.values_num)
	{
		DBbegin();
		DBregister_host_flush(&autoreg_hosts, proxy_hostid);
		DBcommit();
	}

	trx_free(host_metadata);
	DBregister_host_clean(&autoreg_hosts);
	trx_vector_ptr_destroy(&autoreg_hosts);

	if (SUCCEED != ret)
		*error = trx_strdup(*error, trx_json_strerror());

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_auto_registration                                        *
 *                                                                            *
 * Purpose: update auto registration data, received from proxy                *
 *                                                                            *
 * Parameters: jp           - [IN] JSON with historical data                  *
 *             proxy_hostid - [IN] proxy identifier from database             *
 *             ts           - [IN] timestamp when the proxy connection was    *
 *                                 established                                *
 *             error        - [OUT] address of a pointer to the info string   *
 *                                  (should be freed by the caller)           *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
int	process_auto_registration(struct trx_json_parse *jp, trx_uint64_t proxy_hostid, trx_timespec_t *ts,
		char **error)
{
	struct trx_json_parse	jp_data;
	int			ret;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	log_client_timediff(LOG_LEVEL_DEBUG, jp, ts);

	if (SUCCEED != (ret = trx_json_brackets_by_name(jp, TRX_PROTO_TAG_DATA, &jp_data)))
	{
		*error = trx_strdup(*error, trx_json_strerror());
		goto out;
	}

	ret = process_auto_registration_contents(&jp_data, proxy_hostid, error);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: proxy_get_history_count                                          *
 *                                                                            *
 * Purpose: get the number of values waiting to be sent to the sever          *
 *                                                                            *
 * Return value: the number of history values                                 *
 *                                                                            *
 ******************************************************************************/
int	proxy_get_history_count(void)
{
	DB_RESULT	result;
	DB_ROW		row;
	trx_uint64_t	id;
	int		count = 0;

	proxy_get_lastid("proxy_history", "history_lastid", &id);

	result = DBselect(
			"select count(*)"
			" from proxy_history"
			" where id>" TRX_FS_UI64,
			id);

	if (NULL != (row = DBfetch(result)))
		count = atoi(row[0]);

	DBfree_result(result);

	return count;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_get_proxy_protocol_version                                   *
 *                                                                            *
 * Purpose: extracts protocol version from json data                          *
 *                                                                            *
 * Parameters:                                                                *
 *     jp      - [IN] JSON with the proxy version                             *
 *                                                                            *
 * Return value: The protocol version.                                        *
 *     SUCCEED - proxy version was successfully extracted                     *
 *     FAIL    - otherwise                                                    *
 *                                                                            *
 ******************************************************************************/
int	trx_get_proxy_protocol_version(struct trx_json_parse *jp)
{
	char	value[MAX_STRING_LEN];
	int	version;

	if (NULL != jp && SUCCEED == trx_json_value_by_name(jp, TRX_PROTO_TAG_VERSION, value, sizeof(value)) &&
			-1 != (version = trx_get_component_version(value)))
	{
		return version;
	}
	else
		return TRX_COMPONENT_VERSION(3, 2);
}

/******************************************************************************
 *                                                                            *
 * Function: process_tasks_contents                                           *
 *                                                                            *
 * Purpose: parse tasks contents and saves the received tasks                 *
 *                                                                            *
 * Parameters: jp_tasks - [IN] JSON with tasks data                           *
 *                                                                            *
 ******************************************************************************/
static void	process_tasks_contents(struct trx_json_parse *jp_tasks)
{
	trx_vector_ptr_t	tasks;

	trx_vector_ptr_create(&tasks);

	trx_tm_json_deserialize_tasks(jp_tasks, &tasks);

	DBbegin();
	trx_tm_save_tasks(&tasks);
	DBcommit();

	trx_vector_ptr_clear_ext(&tasks, (trx_clean_func_t)trx_tm_task_free);
	trx_vector_ptr_destroy(&tasks);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_strcatnl_alloc                                               *
 *                                                                            *
 * Purpose: appends text to the string on a new line                          *
 *                                                                            *
 ******************************************************************************/
static void	trx_strcatnl_alloc(char **info, size_t *info_alloc, size_t *info_offset, const char *text)
{
	if (0 != *info_offset)
		trx_chrcpy_alloc(info, info_alloc, info_offset, '\n');

	trx_strcpy_alloc(info, info_alloc, info_offset, text);
}

/******************************************************************************
 *                                                                            *
 * Function: process_proxy_data                                               *
 *                                                                            *
 * Purpose: process 'proxy data' request                                      *
 *                                                                            *
 * Parameters: proxy        - [IN] the source proxy                           *
 *             jp           - [IN] JSON with proxy data                       *
 *             proxy_hostid - [IN] proxy identifier from database             *
 *             ts           - [IN] timestamp when the proxy connection was    *
 *                                 established                                *
 *             error        - [OUT] address of a pointer to the info string   *
 *                                  (should be freed by the caller)           *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
int	process_proxy_data(const DC_PROXY *proxy, struct trx_json_parse *jp, trx_timespec_t *ts, char **error)
{
	struct trx_json_parse	jp_data;
	int			ret = SUCCEED;
	char			*error_step = NULL;
	size_t			error_alloc = 0, error_offset = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	log_client_timediff(LOG_LEVEL_DEBUG, jp, ts);

	if (SUCCEED == trx_json_brackets_by_name(jp, TRX_PROTO_TAG_HOST_AVAILABILITY, &jp_data))
	{
		if (SUCCEED != (ret = process_host_availability_contents(&jp_data, &error_step)))
			trx_strcatnl_alloc(error, &error_alloc, &error_offset, error_step);
	}

	if (SUCCEED == trx_json_brackets_by_name(jp, TRX_PROTO_TAG_HISTORY_DATA, &jp_data))
	{
		char			*token = NULL;
		size_t			token_alloc = 0;
		trx_data_session_t	*session = NULL;

		if (SUCCEED == trx_json_value_by_name_dyn(jp, TRX_PROTO_TAG_SESSION, &token, &token_alloc))
		{
			size_t	token_len;

			if (TRX_DATA_SESSION_TOKEN_SIZE != (token_len = strlen(token)))
			{
				*error = trx_dsprintf(*error, "invalid session token length %d", (int)token_len);
				trx_free(token);
				ret = FAIL;
				goto out;
			}

			session = trx_dc_get_or_create_data_session(proxy->hostid, token);
			trx_free(token);
		}

		if (SUCCEED != (ret = process_history_data_by_itemids(NULL, proxy_item_validator,
				(void *)&proxy->hostid, &jp_data, session, &error_step)))
		{
			trx_strcatnl_alloc(error, &error_alloc, &error_offset, error_step);
		}
	}

	if (SUCCEED == trx_json_brackets_by_name(jp, TRX_PROTO_TAG_DISCOVERY_DATA, &jp_data))
	{
		if (SUCCEED != (ret = process_discovery_data_contents(&jp_data, &error_step)))
			trx_strcatnl_alloc(error, &error_alloc, &error_offset, error_step);
	}

	if (SUCCEED == trx_json_brackets_by_name(jp, TRX_PROTO_TAG_AUTO_REGISTRATION, &jp_data))
	{
		if (SUCCEED != (ret = process_auto_registration_contents(&jp_data, proxy->hostid, &error_step)))
			trx_strcatnl_alloc(error, &error_alloc, &error_offset, error_step);
	}

	if (SUCCEED == trx_json_brackets_by_name(jp, TRX_PROTO_TAG_TASKS, &jp_data))
		process_tasks_contents(&jp_data);

out:
	trx_free(error_step);
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_flush_proxy_lastaccess                                    *
 *                                                                            *
 * Purpose: flushes lastaccess changes for proxies every                      *
 *          TRX_PROXY_LASTACCESS_UPDATE_FREQUENCY seconds                     *
 *                                                                            *
 ******************************************************************************/
static void	trx_db_flush_proxy_lastaccess(void)
{
	trx_vector_uint64_pair_t	lastaccess;

	trx_vector_uint64_pair_create(&lastaccess);

	trx_dc_get_proxy_lastaccess(&lastaccess);

	if (0 != lastaccess.values_num)
	{
		char	*sql;
		size_t	sql_alloc = 256, sql_offset = 0;
		int	i;

		sql = (char *)trx_malloc(NULL, sql_alloc);

		DBbegin();
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

		for (i = 0; i < lastaccess.values_num; i++)
		{
			trx_uint64_pair_t	*pair = &lastaccess.values[i];

			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update hosts"
					" set lastaccess=%d"
					" where hostid=" TRX_FS_UI64 ";\n",
					(int)pair->second, pair->first);

			DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
		}

		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (16 < sql_offset)	/* in ORACLE always present begin..end; */
			DBexecute("%s", sql);

		DBcommit();

		trx_free(sql);
	}

	trx_vector_uint64_pair_destroy(&lastaccess);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_update_proxy_data                                            *
 *                                                                            *
 * Purpose: updates proxy runtime properties in cache and database.           *
 *                                                                            *
 * Parameters: proxy      - [IN/OUT] the proxy                                *
 *             version    - [IN] the proxy version                            *
 *             lastaccess - [IN] the last proxy access time                   *
 *             compress   - [IN] 1 if proxy is using data compression,        *
 *                               0 otherwise                                  *
 *                                                                            *
 * Comments: The proxy parameter properties are also updated.                 *
 *                                                                            *
 ******************************************************************************/
void	trx_update_proxy_data(DC_PROXY *proxy, int version, int lastaccess, int compress)
{
	trx_proxy_diff_t	diff;

	diff.hostid = proxy->hostid;
	diff.flags = TRX_FLAGS_PROXY_DIFF_UPDATE;
	diff.version = version;
	diff.lastaccess = lastaccess;
	diff.compress = compress;

	trx_dc_update_proxy(&diff);

	if (0 != (diff.flags & TRX_FLAGS_PROXY_DIFF_UPDATE_VERSION) && 0 != proxy->version)
	{
		treegix_log(LOG_LEVEL_DEBUG, "proxy \"%s\" protocol version updated from %d.%d to %d.%d", proxy->host,
				TRX_COMPONENT_VERSION_MAJOR(proxy->version),
				TRX_COMPONENT_VERSION_MINOR(proxy->version),
				TRX_COMPONENT_VERSION_MAJOR(diff.version),
				TRX_COMPONENT_VERSION_MINOR(diff.version));
	}

	proxy->version = version;
	proxy->auto_compress = compress;
	proxy->lastaccess = lastaccess;

	if (0 != (diff.flags & TRX_FLAGS_PROXY_DIFF_UPDATE_COMPRESS))
		DBexecute("update hosts set auto_compress=%d where hostid=" TRX_FS_UI64, diff.compress, diff.hostid);

	trx_db_flush_proxy_lastaccess();
}
/******************************************************************************
 *                                                                            *
 * Function: trx_update_proxy_lasterror                                       *
 *                                                                            *
 * Purpose: flushes last_version_error_time changes runtime                   *
 *          variable for proxies structures                                   *
 *                                                                            *
 ******************************************************************************/
static void	trx_update_proxy_lasterror(DC_PROXY *proxy)
{
	trx_proxy_diff_t	diff;

	diff.hostid = proxy->hostid;
	diff.flags = TRX_FLAGS_PROXY_DIFF_UPDATE_LASTERROR;
	diff.lastaccess = time(NULL);
	diff.last_version_error_time = proxy->last_version_error_time;

	trx_dc_update_proxy(&diff);
}
/******************************************************************************
 *                                                                            *
 * Function: trx_check_protocol_version                                       *
 *                                                                            *
 * Purpose: check server and proxy versions and compatibility rules           *
 *                                                                            *
 * Parameters:                                                                *
 *     proxy        - [IN] the source proxy                                   *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - no compatibility issue                                       *
 *     FAIL    - compatibility check fault                                    *
 *                                                                            *
 ******************************************************************************/
int	trx_check_protocol_version(DC_PROXY *proxy)
{
	int	server_version;
	int	ret = SUCCEED;
	int	now;
	int	print_log = 0;

	/* warn if another proxy version is used and proceed with compatibility rules*/
	if ((server_version = TRX_COMPONENT_VERSION(TREEGIX_VERSION_MAJOR, TREEGIX_VERSION_MINOR)) != proxy->version)
	{
		now = (int)time(NULL);

		if (proxy->last_version_error_time <= now)
		{
			print_log = 1;
			proxy->last_version_error_time = now + 5 * SEC_PER_MIN;
			trx_update_proxy_lasterror(proxy);
		}

		/* don't accept pre 4.2 data */
		if (TRX_COMPONENT_VERSION(4, 2) > proxy->version)
		{
			if (1 == print_log)
			{
				treegix_log(LOG_LEVEL_WARNING, "cannot process proxy \"%s\":"
						" protocol version %d.%d is not supported anymore",
						proxy->host, TRX_COMPONENT_VERSION_MAJOR(proxy->version),
						TRX_COMPONENT_VERSION_MINOR(proxy->version));
			}
			ret = FAIL;
			goto out;
		}

		if (1 == print_log)
		{
			treegix_log(LOG_LEVEL_WARNING, "proxy \"%s\" protocol version %d.%d differs from server version"
					" %d.%d", proxy->host, TRX_COMPONENT_VERSION_MAJOR(proxy->version),
					TRX_COMPONENT_VERSION_MINOR(proxy->version),
					TREEGIX_VERSION_MAJOR, TREEGIX_VERSION_MINOR);
		}

		if (proxy->version > server_version)
		{
			if (1 == print_log)
				treegix_log(LOG_LEVEL_WARNING, "cannot accept proxy data");
			ret = FAIL;
		}

	}
out:
	return ret;
}
