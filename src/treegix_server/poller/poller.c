

#include "common.h"

#include "db.h"
#include "dbcache.h"
#include "daemon.h"
#include "trxserver.h"
#include "trxself.h"
#include "preproc.h"
#include "../events.h"

#include "poller.h"

#include "checks_agent.h"
#include "checks_aggregate.h"
#include "checks_external.h"
#include "checks_internal.h"
#include "checks_simple.h"
#include "checks_snmp.h"
#include "checks_db.h"
#include "checks_ssh.h"
#include "checks_telnet.h"
#include "checks_java.h"
#include "checks_calculated.h"
#include "checks_http.h"
#include "../../libs/trxcrypto/tls.h"
#include "trxjson.h"
#include "trxhttp.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

/******************************************************************************
 *                                                                            *
 * Function: db_host_update_availability                                      *
 *                                                                            *
 * Purpose: write host availability changes into database                     *
 *                                                                            *
 * Parameters: ha    - [IN] the host availability data                        *
 *                                                                            *
 * Return value: SUCCEED - the availability changes were written into db      *
 *               FAIL    - no changes in availability data were detected      *
 *                                                                            *
 ******************************************************************************/
static int	db_host_update_availability(const trx_host_availability_t *ha)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;

	if (SUCCEED == trx_sql_add_host_availability(&sql, &sql_alloc, &sql_offset, ha))
	{
		DBbegin();
		DBexecute("%s", sql);
		DBcommit();

		trx_free(sql);

		return SUCCEED;
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: host_get_availability                                            *
 *                                                                            *
 * Purpose: get host availability data based on the specified agent type      *
 *                                                                            *
 * Parameters: dc_host      - [IN] the host                                   *
 *             type         - [IN] the agent type                             *
 *             availability - [OUT] the host availability data                *
 *                                                                            *
 * Return value: SUCCEED - the host availability data was retrieved           *
 *                         successfully                                       *
 *               FAIL    - failed to retrieve host availability data,         *
 *                         invalid agent type was specified                   *
 *                                                                            *
 ******************************************************************************/
static int	host_get_availability(const DC_HOST *dc_host, unsigned char agent, trx_host_availability_t *ha)
{
	trx_agent_availability_t	*availability = &ha->agents[agent];

	availability->flags = TRX_FLAGS_AGENT_STATUS;

	switch (agent)
	{
		case TRX_AGENT_TREEGIX:
			availability->available = dc_host->available;
			availability->error = trx_strdup(NULL, dc_host->error);
			availability->errors_from = dc_host->errors_from;
			availability->disable_until = dc_host->disable_until;
			break;
		case TRX_AGENT_SNMP:
			availability->available = dc_host->snmp_available;
			availability->error = trx_strdup(NULL, dc_host->snmp_error);
			availability->errors_from = dc_host->snmp_errors_from;
			availability->disable_until = dc_host->snmp_disable_until;
			break;
		case TRX_AGENT_IPMI:
			availability->available = dc_host->ipmi_available;
			availability->error = trx_strdup(NULL, dc_host->ipmi_error);
			availability->errors_from = dc_host->ipmi_errors_from;
			availability->disable_until = dc_host->ipmi_disable_until;
			break;
		case TRX_AGENT_JMX:
			availability->available = dc_host->jmx_available;
			availability->error = trx_strdup(NULL, dc_host->jmx_error);
			availability->disable_until = dc_host->jmx_disable_until;
			availability->errors_from = dc_host->jmx_errors_from;
			break;
		default:
			return FAIL;
	}

	ha->hostid = dc_host->hostid;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: host_set_availability                                            *
 *                                                                            *
 * Purpose: sets host availability data based on the specified agent type     *
 *                                                                            *
 * Parameters: dc_host      - [IN] the host                                   *
 *             type         - [IN] the agent type                             *
 *             availability - [IN] the host availability data                 *
 *                                                                            *
 * Return value: SUCCEED - the host availability data was set successfully    *
 *               FAIL    - failed to set host availability data,              *
 *                         invalid agent type was specified                   *
 *                                                                            *
 ******************************************************************************/
static int	host_set_availability(DC_HOST *dc_host, unsigned char agent, const trx_host_availability_t *ha)
{
	const trx_agent_availability_t	*availability = &ha->agents[agent];
	unsigned char			*pavailable;
	int				*perrors_from, *pdisable_until;
	char				*perror;

	switch (agent)
	{
		case TRX_AGENT_TREEGIX:
			pavailable = &dc_host->available;
			perror = dc_host->error;
			perrors_from = &dc_host->errors_from;
			pdisable_until = &dc_host->disable_until;
			break;
		case TRX_AGENT_SNMP:
			pavailable = &dc_host->snmp_available;
			perror = dc_host->snmp_error;
			perrors_from = &dc_host->snmp_errors_from;
			pdisable_until = &dc_host->snmp_disable_until;
			break;
		case TRX_AGENT_IPMI:
			pavailable = &dc_host->ipmi_available;
			perror = dc_host->ipmi_error;
			perrors_from = &dc_host->ipmi_errors_from;
			pdisable_until = &dc_host->ipmi_disable_until;
			break;
		case TRX_AGENT_JMX:
			pavailable = &dc_host->jmx_available;
			perror = dc_host->jmx_error;
			pdisable_until = &dc_host->jmx_disable_until;
			perrors_from = &dc_host->jmx_errors_from;
			break;
		default:
			return FAIL;
	}

	if (0 != (availability->flags & TRX_FLAGS_AGENT_STATUS_AVAILABLE))
		*pavailable = availability->available;

	if (0 != (availability->flags & TRX_FLAGS_AGENT_STATUS_ERROR))
		trx_strlcpy(perror, availability->error, HOST_ERROR_LEN_MAX);

	if (0 != (availability->flags & TRX_FLAGS_AGENT_STATUS_ERRORS_FROM))
		*perrors_from = availability->errors_from;

	if (0 != (availability->flags & TRX_FLAGS_AGENT_STATUS_DISABLE_UNTIL))
		*pdisable_until = availability->disable_until;

	return SUCCEED;
}

static unsigned char	host_availability_agent_by_item_type(unsigned char type)
{
	switch (type)
	{
		case ITEM_TYPE_TREEGIX:
			return TRX_AGENT_TREEGIX;
			break;
		case ITEM_TYPE_SNMPv1:
		case ITEM_TYPE_SNMPv2c:
		case ITEM_TYPE_SNMPv3:
			return TRX_AGENT_SNMP;
			break;
		case ITEM_TYPE_IPMI:
			return TRX_AGENT_IPMI;
			break;
		case ITEM_TYPE_JMX:
			return TRX_AGENT_JMX;
			break;
		default:
			return TRX_AGENT_UNKNOWN;
	}
}

void	trx_activate_item_host(DC_ITEM *item, trx_timespec_t *ts)
{
	trx_host_availability_t	in, out;
	unsigned char		agent_type;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() hostid:" TRX_FS_UI64 " itemid:" TRX_FS_UI64 " type:%d",
			__func__, item->host.hostid, item->itemid, (int)item->type);

	trx_host_availability_init(&in, item->host.hostid);
	trx_host_availability_init(&out, item->host.hostid);

	if (TRX_AGENT_UNKNOWN == (agent_type = host_availability_agent_by_item_type(item->type)))
		goto out;

	if (FAIL == host_get_availability(&item->host, agent_type, &in))
		goto out;

	if (FAIL == DChost_activate(item->host.hostid, agent_type, ts, &in.agents[agent_type], &out.agents[agent_type]))
		goto out;

	if (FAIL == db_host_update_availability(&out))
		goto out;

	host_set_availability(&item->host, agent_type, &out);

	if (HOST_AVAILABLE_TRUE == in.agents[agent_type].available)
	{
		treegix_log(LOG_LEVEL_WARNING, "resuming %s checks on host \"%s\": connection restored",
				trx_agent_type_string(item->type), item->host.host);
	}
	else
	{
		treegix_log(LOG_LEVEL_WARNING, "enabling %s checks on host \"%s\": host became available",
				trx_agent_type_string(item->type), item->host.host);
	}
out:
	trx_host_availability_clean(&out);
	trx_host_availability_clean(&in);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

void	trx_deactivate_item_host(DC_ITEM *item, trx_timespec_t *ts, const char *error)
{
	trx_host_availability_t	in, out;
	unsigned char		agent_type;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() hostid:" TRX_FS_UI64 " itemid:" TRX_FS_UI64 " type:%d",
			__func__, item->host.hostid, item->itemid, (int)item->type);

	trx_host_availability_init(&in, item->host.hostid);
	trx_host_availability_init(&out,item->host.hostid);

	if (TRX_AGENT_UNKNOWN == (agent_type = host_availability_agent_by_item_type(item->type)))
		goto out;

	if (FAIL == host_get_availability(&item->host, agent_type, &in))
		goto out;

	if (FAIL == DChost_deactivate(item->host.hostid, agent_type, ts, &in.agents[agent_type],
			&out.agents[agent_type], error))
	{
		goto out;
	}

	if (FAIL == db_host_update_availability(&out))
		goto out;

	host_set_availability(&item->host, agent_type, &out);

	if (0 == in.agents[agent_type].errors_from)
	{
		treegix_log(LOG_LEVEL_WARNING, "%s item \"%s\" on host \"%s\" failed:"
				" first network error, wait for %d seconds",
				trx_agent_type_string(item->type), item->key_orig, item->host.host,
				out.agents[agent_type].disable_until - ts->sec);
	}
	else
	{
		if (HOST_AVAILABLE_FALSE != in.agents[agent_type].available)
		{
			if (HOST_AVAILABLE_FALSE != out.agents[agent_type].available)
			{
				treegix_log(LOG_LEVEL_WARNING, "%s item \"%s\" on host \"%s\" failed:"
						" another network error, wait for %d seconds",
						trx_agent_type_string(item->type), item->key_orig, item->host.host,
						out.agents[agent_type].disable_until - ts->sec);
			}
			else
			{
				treegix_log(LOG_LEVEL_WARNING, "temporarily disabling %s checks on host \"%s\":"
						" host unavailable",
						trx_agent_type_string(item->type), item->host.host);
			}
		}
	}

	treegix_log(LOG_LEVEL_DEBUG, "%s() errors_from:%d available:%d", __func__,
			out.agents[agent_type].errors_from, out.agents[agent_type].available);
out:
	trx_host_availability_clean(&out);
	trx_host_availability_clean(&in);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	free_result_ptr(AGENT_RESULT *result)
{
	free_result(result);
	trx_free(result);
}

static int	get_value(DC_ITEM *item, AGENT_RESULT *result, trx_vector_ptr_t *add_results)
{
	int	res = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() key:'%s'", __func__, item->key_orig);

	switch (item->type)
	{
		case ITEM_TYPE_TREEGIX:
			trx_alarm_on(CONFIG_TIMEOUT);
			res = get_value_agent(item, result);
			trx_alarm_off();
			break;
		case ITEM_TYPE_SIMPLE:
			/* simple checks use their own timeouts */
			res = get_value_simple(item, result, add_results);
			break;
		case ITEM_TYPE_INTERNAL:
			res = get_value_internal(item, result);
			break;
		case ITEM_TYPE_DB_MONITOR:
#ifdef HAVE_UNIXODBC
			res = get_value_db(item, result);
#else
			SET_MSG_RESULT(result,
					trx_strdup(NULL, "Support for Database monitor checks was not compiled in."));
			res = CONFIG_ERROR;
#endif
			break;
		case ITEM_TYPE_AGGREGATE:
			res = get_value_aggregate(item, result);
			break;
		case ITEM_TYPE_EXTERNAL:
			/* external checks use their own timeouts */
			res = get_value_external(item, result);
			break;
		case ITEM_TYPE_SSH:
#ifdef HAVE_SSH2
			trx_alarm_on(CONFIG_TIMEOUT);
			res = get_value_ssh(item, result);
			trx_alarm_off();
#else
			SET_MSG_RESULT(result, trx_strdup(NULL, "Support for SSH checks was not compiled in."));
			res = CONFIG_ERROR;
#endif
			break;
		case ITEM_TYPE_TELNET:
			trx_alarm_on(CONFIG_TIMEOUT);
			res = get_value_telnet(item, result);
			trx_alarm_off();
			break;
		case ITEM_TYPE_CALCULATED:
			res = get_value_calculated(item, result);
			break;
		case ITEM_TYPE_HTTPAGENT:
#ifdef HAVE_LIBCURL
			res = get_value_http(item, result);
#else
			SET_MSG_RESULT(result, trx_strdup(NULL, "Support for HTTP agent checks was not compiled in."));
			res = CONFIG_ERROR;
#endif
			break;
		default:
			SET_MSG_RESULT(result, trx_dsprintf(NULL, "Not supported item type:%d", item->type));
			res = CONFIG_ERROR;
	}

	if (SUCCEED != res)
	{
		if (!ISSET_MSG(result))
			SET_MSG_RESULT(result, trx_strdup(NULL, TRX_NOTSUPPORTED_MSG));

		treegix_log(LOG_LEVEL_DEBUG, "Item [%s:%s] error: %s", item->host.host, item->key_orig, result->msg);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(res));

	return res;
}

static int	parse_query_fields(const DC_ITEM *item, char **query_fields)
{
	struct trx_json_parse	jp_array, jp_object;
	char			name[MAX_STRING_LEN], value[MAX_STRING_LEN];
	const char		*member, *element = NULL;
	size_t			alloc_len, offset;

	if ('\0' == *item->query_fields_orig)
	{
		TRX_STRDUP(*query_fields, item->query_fields_orig);
		return SUCCEED;
	}

	if (SUCCEED != trx_json_open(item->query_fields_orig, &jp_array))
	{
		treegix_log(LOG_LEVEL_ERR, "cannot parse query fields: %s", trx_json_strerror());
		return FAIL;
	}

	if (NULL == (element = trx_json_next(&jp_array, element)))
	{
		treegix_log(LOG_LEVEL_ERR, "cannot parse query fields: array is empty");
		return FAIL;
	}

	do
	{
		char	*data = NULL;

		if (SUCCEED != trx_json_brackets_open(element, &jp_object) ||
				NULL == (member = trx_json_pair_next(&jp_object, NULL, name, sizeof(name))) ||
				NULL == trx_json_decodevalue(member, value, sizeof(value), NULL))
		{
			treegix_log(LOG_LEVEL_ERR, "cannot parse query fields: %s", trx_json_strerror());
			return FAIL;
		}

		if (NULL == *query_fields && NULL == strchr(item->url, '?'))
			trx_chrcpy_alloc(query_fields, &alloc_len, &offset, '?');
		else
			trx_chrcpy_alloc(query_fields, &alloc_len, &offset, '&');

		data = trx_strdup(data, name);
		substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &item->host, item, NULL, NULL, &data,
				MACRO_TYPE_HTTP_RAW, NULL, 0);
		trx_http_url_encode(data, &data);
		trx_strcpy_alloc(query_fields, &alloc_len, &offset, data);
		trx_chrcpy_alloc(query_fields, &alloc_len, &offset, '=');

		data = trx_strdup(data, value);
		substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &item->host, item, NULL, NULL, &data,
				MACRO_TYPE_HTTP_RAW, NULL, 0);
		trx_http_url_encode(data, &data);
		trx_strcpy_alloc(query_fields, &alloc_len, &offset, data);

		free(data);
	}
	while (NULL != (element = trx_json_next(&jp_array, element)));

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: get_values                                                       *
 *                                                                            *
 * Purpose: retrieve values of metrics from monitored hosts                   *
 *                                                                            *
 * Parameters: poller_type - [IN] poller type (TRX_POLLER_TYPE_...)           *
 *                                                                            *
 * Return value: number of items processed                                    *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: processes single item at a time except for Java, SNMP items,     *
 *           see DCconfig_get_poller_items()                                  *
 *                                                                            *
 ******************************************************************************/
static int	get_values(unsigned char poller_type, int *nextcheck)
{
	DC_ITEM			items[MAX_POLLER_ITEMS];
	AGENT_RESULT		results[MAX_POLLER_ITEMS];
	int			errcodes[MAX_POLLER_ITEMS];
	trx_timespec_t		timespec;
	char			*port = NULL, error[ITEM_ERROR_LEN_MAX];
	int			i, num, last_available = HOST_AVAILABLE_UNKNOWN;
	trx_vector_ptr_t	add_results;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	num = DCconfig_get_poller_items(poller_type, items);

	if (0 == num)
	{
		*nextcheck = DCconfig_get_poller_nextcheck(poller_type);
		goto exit;
	}

	/* prepare items */
	for (i = 0; i < num; i++)
	{
		init_result(&results[i]);
		errcodes[i] = SUCCEED;

		TRX_STRDUP(items[i].key, items[i].key_orig);
		if (SUCCEED != substitute_key_macros(&items[i].key, NULL, &items[i], NULL, NULL,
				MACRO_TYPE_ITEM_KEY, error, sizeof(error)))
		{
			SET_MSG_RESULT(&results[i], trx_strdup(NULL, error));
			errcodes[i] = CONFIG_ERROR;
			continue;
		}

		switch (items[i].type)
		{
			case ITEM_TYPE_TREEGIX:
			case ITEM_TYPE_SNMPv1:
			case ITEM_TYPE_SNMPv2c:
			case ITEM_TYPE_SNMPv3:
			case ITEM_TYPE_JMX:
				TRX_STRDUP(port, items[i].interface.port_orig);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &port, MACRO_TYPE_COMMON, NULL, 0);
				if (FAIL == is_ushort(port, &items[i].interface.port))
				{
					SET_MSG_RESULT(&results[i], trx_dsprintf(NULL, "Invalid port number [%s]",
								items[i].interface.port_orig));
					errcodes[i] = CONFIG_ERROR;
					continue;
				}
				break;
		}

		switch (items[i].type)
		{
			case ITEM_TYPE_SNMPv3:
				TRX_STRDUP(items[i].snmpv3_securityname, items[i].snmpv3_securityname_orig);
				TRX_STRDUP(items[i].snmpv3_authpassphrase, items[i].snmpv3_authpassphrase_orig);
				TRX_STRDUP(items[i].snmpv3_privpassphrase, items[i].snmpv3_privpassphrase_orig);
				TRX_STRDUP(items[i].snmpv3_contextname, items[i].snmpv3_contextname_orig);

				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].snmpv3_securityname,
						MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].snmpv3_authpassphrase,
						MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].snmpv3_privpassphrase,
						MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].snmpv3_contextname,
						MACRO_TYPE_COMMON, NULL, 0);
				TRX_FALLTHROUGH;
			case ITEM_TYPE_SNMPv1:
			case ITEM_TYPE_SNMPv2c:
				TRX_STRDUP(items[i].snmp_community, items[i].snmp_community_orig);
				TRX_STRDUP(items[i].snmp_oid, items[i].snmp_oid_orig);

				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].snmp_community, MACRO_TYPE_COMMON, NULL, 0);
				if (SUCCEED != substitute_key_macros(&items[i].snmp_oid, &items[i].host.hostid, NULL,
						NULL, NULL, MACRO_TYPE_SNMP_OID, error, sizeof(error)))
				{
					SET_MSG_RESULT(&results[i], trx_strdup(NULL, error));
					errcodes[i] = CONFIG_ERROR;
					continue;
				}
				break;
			case ITEM_TYPE_SSH:
				TRX_STRDUP(items[i].publickey, items[i].publickey_orig);
				TRX_STRDUP(items[i].privatekey, items[i].privatekey_orig);

				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].publickey, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].privatekey, MACRO_TYPE_COMMON, NULL, 0);
				TRX_FALLTHROUGH;
			case ITEM_TYPE_TELNET:
			case ITEM_TYPE_DB_MONITOR:
				substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, &items[i],
						NULL, NULL, &items[i].params, MACRO_TYPE_PARAMS_FIELD, NULL, 0);
				TRX_FALLTHROUGH;
			case ITEM_TYPE_SIMPLE:
				items[i].username = trx_strdup(items[i].username, items[i].username_orig);
				items[i].password = trx_strdup(items[i].password, items[i].password_orig);

				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].username, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].password, MACRO_TYPE_COMMON, NULL, 0);
				break;
			case ITEM_TYPE_JMX:
				items[i].username = trx_strdup(items[i].username, items[i].username_orig);
				items[i].password = trx_strdup(items[i].password, items[i].password_orig);
				items[i].jmx_endpoint = trx_strdup(items[i].jmx_endpoint, items[i].jmx_endpoint_orig);

				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].username, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].password, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, &items[i],
						NULL, NULL, &items[i].jmx_endpoint, MACRO_TYPE_JMX_ENDPOINT, NULL, 0);
				break;
			case ITEM_TYPE_HTTPAGENT:
				TRX_STRDUP(items[i].timeout, items[i].timeout_orig);
				TRX_STRDUP(items[i].url, items[i].url_orig);
				TRX_STRDUP(items[i].status_codes, items[i].status_codes_orig);
				TRX_STRDUP(items[i].http_proxy, items[i].http_proxy_orig);
				TRX_STRDUP(items[i].ssl_cert_file, items[i].ssl_cert_file_orig);
				TRX_STRDUP(items[i].ssl_key_file, items[i].ssl_key_file_orig);
				TRX_STRDUP(items[i].ssl_key_password, items[i].ssl_key_password_orig);
				TRX_STRDUP(items[i].username, items[i].username_orig);
				TRX_STRDUP(items[i].password, items[i].password_orig);

				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].timeout, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &items[i].host, &items[i], NULL,
						NULL, &items[i].url, MACRO_TYPE_HTTP_RAW, NULL, 0);

				if (SUCCEED != trx_http_punycode_encode_url(&items[i].url))
				{
					SET_MSG_RESULT(&results[i], trx_strdup(NULL, "Cannot encode URL into punycode"));
					errcodes[i] = CONFIG_ERROR;
					continue;
				}

				if (FAIL == parse_query_fields(&items[i], &items[i].query_fields))
				{
					SET_MSG_RESULT(&results[i], trx_strdup(NULL, "Invalid query fields"));
					errcodes[i] = CONFIG_ERROR;
					continue;
				}

				switch (items[i].post_type)
				{
					case TRX_POSTTYPE_XML:
						if (SUCCEED != substitute_macros_xml(&items[i].posts, &items[i], NULL,
								NULL, error, sizeof(error)))
						{
							SET_MSG_RESULT(&results[i], trx_dsprintf(NULL, "%s.", error));
							errcodes[i] = CONFIG_ERROR;
							continue;
						}
						break;
					case TRX_POSTTYPE_JSON:
						substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &items[i].host,
								&items[i], NULL, NULL, &items[i].posts,
								MACRO_TYPE_HTTP_JSON, NULL, 0);
						break;
					default:
						substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &items[i].host,
								&items[i], NULL, NULL, &items[i].posts,
								MACRO_TYPE_HTTP_RAW, NULL, 0);
						break;
				}

				substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &items[i].host, &items[i], NULL,
						NULL, &items[i].headers, MACRO_TYPE_HTTP_RAW, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].status_codes, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].http_proxy, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &items[i].host, &items[i], NULL,
						NULL, &items[i].ssl_cert_file, MACRO_TYPE_HTTP_RAW, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &items[i].host, &items[i], NULL,
						NULL, &items[i].ssl_key_file, MACRO_TYPE_HTTP_RAW, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL, NULL,
						NULL, NULL, &items[i].ssl_key_password, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].username, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].password, MACRO_TYPE_COMMON, NULL, 0);
				break;
		}
	}

	trx_free(port);

	trx_vector_ptr_create(&add_results);

	/* retrieve item values */
	if (SUCCEED == is_snmp_type(items[0].type))
	{
#ifdef HAVE_NETSNMP
		/* SNMP checks use their own timeouts */
		get_values_snmp(items, results, errcodes, num);
#else
		for (i = 0; i < num; i++)
		{
			if (SUCCEED != errcodes[i])
				continue;

			SET_MSG_RESULT(&results[i], trx_strdup(NULL, "Support for SNMP checks was not compiled in."));
			errcodes[i] = CONFIG_ERROR;
		}
#endif
	}
	else if (ITEM_TYPE_JMX == items[0].type)
	{
		trx_alarm_on(CONFIG_TIMEOUT);
		get_values_java(TRX_JAVA_GATEWAY_REQUEST_JMX, items, results, errcodes, num);
		trx_alarm_off();
	}
	else if (1 == num)
	{
		if (SUCCEED == errcodes[0])
			errcodes[0] = get_value(&items[0], &results[0], &add_results);
	}
	else
		THIS_SHOULD_NEVER_HAPPEN;

	trx_timespec(&timespec);

	/* process item values */
	for (i = 0; i < num; i++)
	{
		switch (errcodes[i])
		{
			case SUCCEED:
			case NOTSUPPORTED:
			case AGENT_ERROR:
				if (HOST_AVAILABLE_TRUE != last_available)
				{
					trx_activate_item_host(&items[i], &timespec);
					last_available = HOST_AVAILABLE_TRUE;
				}
				break;
			case NETWORK_ERROR:
			case GATEWAY_ERROR:
			case TIMEOUT_ERROR:
				if (HOST_AVAILABLE_FALSE != last_available)
				{
					trx_deactivate_item_host(&items[i], &timespec, results[i].msg);
					last_available = HOST_AVAILABLE_FALSE;
				}
				break;
			case CONFIG_ERROR:
				/* nothing to do */
				break;
			default:
				trx_error("unknown response code returned: %d", errcodes[i]);
				THIS_SHOULD_NEVER_HAPPEN;
		}

		if (SUCCEED == errcodes[i])
		{
			if (0 == add_results.values_num)
			{
				items[i].state = ITEM_STATE_NORMAL;
				trx_preprocess_item_value(items[i].itemid, items[i].value_type, items[i].flags,
						&results[i], &timespec, items[i].state, NULL);
			}
			else
			{
				/* vmware.eventlog item returns vector of AGENT_RESULT representing events */

				int		j;
				trx_timespec_t	ts_tmp = timespec;

				for (j = 0; j < add_results.values_num; j++)
				{
					AGENT_RESULT	*add_result = (AGENT_RESULT *)add_results.values[j];

					if (ISSET_MSG(add_result))
					{
						items[i].state = ITEM_STATE_NOTSUPPORTED;
						trx_preprocess_item_value(items[i].itemid, items[i].value_type,
								items[i].flags, NULL, &ts_tmp, items[i].state,
								add_result->msg);
					}
					else
					{
						items[i].state = ITEM_STATE_NORMAL;
						trx_preprocess_item_value(items[i].itemid, items[i].value_type,
								items[i].flags, add_result, &ts_tmp, items[i].state,
								NULL);
					}

					/* ensure that every log item value timestamp is unique */
					if (++ts_tmp.ns == 1000000000)
					{
						ts_tmp.sec++;
						ts_tmp.ns = 0;
					}
				}
			}
		}
		else if (NOTSUPPORTED == errcodes[i] || AGENT_ERROR == errcodes[i] || CONFIG_ERROR == errcodes[i])
		{
			items[i].state = ITEM_STATE_NOTSUPPORTED;
			trx_preprocess_item_value(items[i].itemid, items[i].value_type, items[i].flags, NULL, &timespec,
					items[i].state, results[i].msg);
		}

		DCpoller_requeue_items(&items[i].itemid, &items[i].state, &timespec.sec, &errcodes[i], 1, poller_type,
				nextcheck);

		trx_free(items[i].key);

		switch (items[i].type)
		{
			case ITEM_TYPE_SNMPv3:
				trx_free(items[i].snmpv3_securityname);
				trx_free(items[i].snmpv3_authpassphrase);
				trx_free(items[i].snmpv3_privpassphrase);
				trx_free(items[i].snmpv3_contextname);
				TRX_FALLTHROUGH;
			case ITEM_TYPE_SNMPv1:
			case ITEM_TYPE_SNMPv2c:
				trx_free(items[i].snmp_community);
				trx_free(items[i].snmp_oid);
				break;
			case ITEM_TYPE_HTTPAGENT:
				trx_free(items[i].timeout);
				trx_free(items[i].url);
				trx_free(items[i].query_fields);
				trx_free(items[i].status_codes);
				trx_free(items[i].http_proxy);
				trx_free(items[i].ssl_cert_file);
				trx_free(items[i].ssl_key_file);
				trx_free(items[i].ssl_key_password);
				trx_free(items[i].username);
				trx_free(items[i].password);
				break;
			case ITEM_TYPE_SSH:
				trx_free(items[i].publickey);
				trx_free(items[i].privatekey);
				TRX_FALLTHROUGH;
			case ITEM_TYPE_TELNET:
			case ITEM_TYPE_DB_MONITOR:
			case ITEM_TYPE_SIMPLE:
				trx_free(items[i].username);
				trx_free(items[i].password);
				break;
			case ITEM_TYPE_JMX:
				trx_free(items[i].username);
				trx_free(items[i].password);
				trx_free(items[i].jmx_endpoint);
				break;
		}

		free_result(&results[i]);
	}

	trx_preprocessor_flush();
	trx_vector_ptr_clear_ext(&add_results, (trx_mem_free_func_t)free_result_ptr);
	trx_vector_ptr_destroy(&add_results);

	DCconfig_clean_items(items, NULL, num);
exit:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, num);

	return num;
}

TRX_THREAD_ENTRY(poller_thread, args)
{
	int		nextcheck, sleeptime = -1, processed = 0, old_processed = 0;
	double		sec, total_sec = 0.0, old_total_sec = 0.0;
	time_t		last_stat_time;
	unsigned char	poller_type;

#define	STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

	poller_type = *(unsigned char *)((trx_thread_args_t *)args)->args;
	process_type = ((trx_thread_args_t *)args)->process_type;

	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);
#ifdef HAVE_NETSNMP
	if (TRX_POLLER_TYPE_NORMAL == poller_type || TRX_POLLER_TYPE_UNREACHABLE == poller_type)
		trx_init_snmp();
#endif

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	trx_tls_init_child();
#endif
	trx_setproctitle("%s #%d [connecting to the database]", get_process_type_string(process_type), process_num);
	last_stat_time = time(NULL);

	DBconnect(TRX_DB_CONNECT_NORMAL);

	while (TRX_IS_RUNNING())
	{
		sec = trx_time();
		trx_update_env(sec);

		if (0 != sleeptime)
		{
			trx_setproctitle("%s #%d [got %d values in " TRX_FS_DBL " sec, getting values]",
					get_process_type_string(process_type), process_num, old_processed,
					old_total_sec);
		}

		processed += get_values(poller_type, &nextcheck);
		total_sec += trx_time() - sec;

		sleeptime = calculate_sleeptime(nextcheck, POLLER_DELAY);

		if (0 != sleeptime || STAT_INTERVAL <= time(NULL) - last_stat_time)
		{
			if (0 == sleeptime)
			{
				trx_setproctitle("%s #%d [got %d values in " TRX_FS_DBL " sec, getting values]",
					get_process_type_string(process_type), process_num, processed, total_sec);
			}
			else
			{
				trx_setproctitle("%s #%d [got %d values in " TRX_FS_DBL " sec, idle %d sec]",
					get_process_type_string(process_type), process_num, processed, total_sec,
					sleeptime);
				old_processed = processed;
				old_total_sec = total_sec;
			}
			processed = 0;
			total_sec = 0.0;
			last_stat_time = time(NULL);
		}

		trx_sleep_loop(sleeptime);
	}

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);
#undef STAT_INTERVAL
}
