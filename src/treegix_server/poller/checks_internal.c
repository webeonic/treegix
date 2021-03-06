

#include "common.h"
#include "checks_internal.h"
#include "checks_java.h"
#include "dbcache.h"
#include "trxself.h"
#include "proxy.h"

#include "../vmware/vmware.h"
#include "../../libs/trxserver/treegix_stats.h"
#include "../../libs/trxsysinfo/common/treegix_stats.h"

extern unsigned char	program_type;

static int	compare_interfaces(const void *p1, const void *p2)
{
	const DC_INTERFACE2	*i1 = (DC_INTERFACE2 *)p1, *i2 = (DC_INTERFACE2 *)p2;

	if (i1->type > i2->type)		/* 1st criterion: 'type' in ascending order */
		return 1;

	if (i1->type < i2->type)
		return -1;

	if (i1->main > i2->main)		/* 2nd criterion: 'main' in descending order */
		return -1;

	if (i1->main < i2->main)
		return 1;

	if (i1->interfaceid > i2->interfaceid)	/* 3rd criterion: 'interfaceid' in ascending order */
		return 1;

	if (i1->interfaceid < i2->interfaceid)
		return -1;

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_host_interfaces_discovery                                    *
 *                                                                            *
 * Purpose: get data of all network interfaces for a host from configuration  *
 *          cache and pack into JSON for LLD                                  *
 *                                                                            *
 * Parameter: hostid - [IN] the host identifier                               *
 *            j      - [OUT] JSON with interface data                         *
 *            error  - [OUT] error message                                    *
 *                                                                            *
 * Return value: SUCCEED - interface data in JSON                             *
 *               FAIL    - host not found, 'error' message allocated          *
 *                                                                            *
 * Comments: if host is found but has no interfaces (should not happen) an    *
 *           empty JSON {"data":[]} is returned                               *
 *                                                                            *
 ******************************************************************************/
static int	trx_host_interfaces_discovery(trx_uint64_t hostid, struct trx_json *j, char **error)
{
	DC_INTERFACE2	*interfaces = NULL;
	int		n = 0;			/* number of interfaces */
	int		i;

	/* get interface data from configuration cache */

	if (SUCCEED != trx_dc_get_host_interfaces(hostid, &interfaces, &n))
	{
		*error = trx_strdup(*error, "host not found in configuration cache");

		return FAIL;
	}

	/* sort results in a predictable order */

	if (1 < n)
		qsort(interfaces, (size_t)n, sizeof(DC_INTERFACE2), compare_interfaces);

	/* repair 'addr' pointers broken by sorting */

	for (i = 0; i < n; i++)
		interfaces[i].addr = (1 == interfaces[i].useip ? interfaces[i].ip_orig : interfaces[i].dns_orig);

	/* pack results into JSON */

	trx_json_initarray(j, TRX_JSON_STAT_BUF_LEN);

	for (i = 0; i < n; i++)
	{
		const char	*p;
		char		buf[16];

		trx_json_addobject(j, NULL);
		trx_json_addstring(j, "{#IF.CONN}", interfaces[i].addr, TRX_JSON_TYPE_STRING);
		trx_json_addstring(j, "{#IF.IP}", interfaces[i].ip_orig, TRX_JSON_TYPE_STRING);
		trx_json_addstring(j, "{#IF.DNS}", interfaces[i].dns_orig, TRX_JSON_TYPE_STRING);
		trx_json_addstring(j, "{#IF.PORT}", interfaces[i].port_orig, TRX_JSON_TYPE_STRING);

		switch (interfaces[i].type)
		{
			case INTERFACE_TYPE_AGENT:
				p = "AGENT";
				break;
			case INTERFACE_TYPE_SNMP:
				p = "SNMP";
				break;
			case INTERFACE_TYPE_IPMI:
				p = "IPMI";
				break;
			case INTERFACE_TYPE_JMX:
				p = "JMX";
				break;
			case INTERFACE_TYPE_UNKNOWN:
			default:
				p = "UNKNOWN";
		}
		trx_json_addstring(j, "{#IF.TYPE}", p, TRX_JSON_TYPE_STRING);

		trx_snprintf(buf, sizeof(buf), "%hhu", interfaces[i].main);
		trx_json_addstring(j, "{#IF.DEFAULT}", buf, TRX_JSON_TYPE_INT);

		if (INTERFACE_TYPE_SNMP == interfaces[i].type)
		{
			trx_snprintf(buf, sizeof(buf), "%hhu", interfaces[i].bulk);
			trx_json_addstring(j, "{#IF.SNMP.BULK}", buf, TRX_JSON_TYPE_INT);
		}

		trx_json_close(j);
	}

	trx_json_close(j);

	trx_free(interfaces);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: get_value_internal                                               *
 *                                                                            *
 * Purpose: retrieve data from Treegix server (internally supported items)     *
 *                                                                            *
 * Parameters: item - item we are interested in                               *
 *                                                                            *
 * Return value: SUCCEED - data successfully retrieved and stored in result   *
 *               NOTSUPPORTED - requested item is not supported               *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
int	get_value_internal(DC_ITEM *item, AGENT_RESULT *result)
{
	AGENT_REQUEST	request;
	int		ret = NOTSUPPORTED, nparams;
	const char	*tmp, *tmp1;

	init_request(&request);

	if (SUCCEED != parse_item_key(item->key, &request))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid item key format."));
		goto out;
	}

	if (0 != strcmp("treegix", get_rkey(&request)))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Unsupported item key for this item type."));
		goto out;
	}

	/* NULL check to silence analyzer warning */
	if (0 == (nparams = get_rparams_num(&request)) || NULL == (tmp = get_rparam(&request, 0)))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
		goto out;
	}

	if (FAIL != (ret = trx_get_value_internal_ext(tmp, &request, result)))
		goto out;

	ret = NOTSUPPORTED;

	if (0 == strcmp(tmp, "items"))			/* treegix["items"] */
	{
		if (1 != nparams)
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
			goto out;
		}

		SET_UI64_RESULT(result, DCget_item_count(0));
	}
	else if (0 == strcmp(tmp, "items_unsupported"))		/* treegix["items_unsupported"] */
	{
		if (1 != nparams)
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
			goto out;
		}

		SET_UI64_RESULT(result, DCget_item_unsupported_count(0));
	}
	else if (0 == strcmp(tmp, "hosts"))			/* treegix["hosts"] */
	{
		if (1 != nparams)
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
			goto out;
		}

		SET_UI64_RESULT(result, DCget_host_count());
	}
	else if (0 == strcmp(tmp, "queue"))			/* treegix["queue",<from>,<to>] */
	{
		int	from = TRX_QUEUE_FROM_DEFAULT, to = TRX_QUEUE_TO_INFINITY;

		if (3 < nparams)
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
			goto out;
		}

		if (NULL != (tmp = get_rparam(&request, 1)) && '\0' != *tmp &&
				FAIL == is_time_suffix(tmp, &from, TRX_LENGTH_UNLIMITED))
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
			goto out;
		}

		if (NULL != (tmp = get_rparam(&request, 2)) && '\0' != *tmp &&
				FAIL == is_time_suffix(tmp, &to, TRX_LENGTH_UNLIMITED))
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid third parameter."));
			goto out;
		}

		if (TRX_QUEUE_TO_INFINITY != to && from > to)
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Parameters represent an invalid interval."));
			goto out;
		}

		SET_UI64_RESULT(result, DCget_item_queue(NULL, from, to));
	}
	else if (0 == strcmp(tmp, "requiredperformance"))	/* treegix["requiredperformance"] */
	{
		if (1 != nparams)
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
			goto out;
		}

		SET_DBL_RESULT(result, DCget_required_performance());
	}
	else if (0 == strcmp(tmp, "uptime"))			/* treegix["uptime"] */
	{
		if (1 != nparams)
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
			goto out;
		}

		SET_UI64_RESULT(result, time(NULL) - CONFIG_SERVER_STARTUP_TIME);
	}
	else if (0 == strcmp(tmp, "boottime"))			/* treegix["boottime"] */
	{
		if (1 != nparams)
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
			goto out;
		}

		SET_UI64_RESULT(result, CONFIG_SERVER_STARTUP_TIME);
	}
	else if (0 == strcmp(tmp, "host"))			/* treegix["host",*] */
	{
		if (3 != nparams)
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
			goto out;
		}

		tmp = get_rparam(&request, 2);

		if (0 == strcmp(tmp, "available"))		/* treegix["host",<type>,"available"] */
		{
			tmp = get_rparam(&request, 1);

			if (0 == strcmp(tmp, "agent"))
				SET_UI64_RESULT(result, item->host.available);
			else if (0 == strcmp(tmp, "snmp"))
				SET_UI64_RESULT(result, item->host.snmp_available);
			else if (0 == strcmp(tmp, "ipmi"))
				SET_UI64_RESULT(result, item->host.ipmi_available);
			else if (0 == strcmp(tmp, "jmx"))
				SET_UI64_RESULT(result, item->host.jmx_available);
			else
			{
				SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
				goto out;
			}

			result->ui64 = 2 - result->ui64;
		}
		else if (0 == strcmp(tmp, "maintenance"))	/* treegix["host",,"maintenance"] */
		{
			/* this item is always processed by server */
			if (NULL != (tmp = get_rparam(&request, 1)) && '\0' != *tmp)
			{
				SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
				goto out;
			}

			if (HOST_MAINTENANCE_STATUS_ON == item->host.maintenance_status)
				SET_UI64_RESULT(result, item->host.maintenance_type + 1);
			else
				SET_UI64_RESULT(result, 0);
		}
		else if (0 == strcmp(tmp, "items"))	/* treegix["host",,"items"] */
		{
			/* this item is always processed by server */
			if (NULL != (tmp = get_rparam(&request, 1)) && '\0' != *tmp)
			{
				SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
				goto out;
			}

			SET_UI64_RESULT(result, DCget_item_count(item->host.hostid));
		}
		else if (0 == strcmp(tmp, "items_unsupported"))	/* treegix["host",,"items_unsupported"] */
		{
			/* this item is always processed by server */
			if (NULL != (tmp = get_rparam(&request, 1)) && '\0' != *tmp)
			{
				SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
				goto out;
			}

			SET_UI64_RESULT(result, DCget_item_unsupported_count(item->host.hostid));
		}
		else if (0 == strcmp(tmp, "interfaces"))	/* treegix["host","discovery","interfaces"] */
		{
			struct trx_json	j;
			char		*error = NULL;

			/* this item is always processed by server */
			if (NULL == (tmp = get_rparam(&request, 1)) || 0 != strcmp(tmp, "discovery"))
			{
				SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
				goto out;
			}

			if (SUCCEED != trx_host_interfaces_discovery(item->host.hostid, &j, &error))
			{
				SET_MSG_RESULT(result, error);
				goto out;
			}

			SET_STR_RESULT(result, trx_strdup(NULL, j.buffer));

			trx_json_free(&j);
		}
		else
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid third parameter."));
			goto out;
		}
	}
	else if (0 == strcmp(tmp, "java"))			/* treegix["java",...] */
	{
		int	res;

		trx_alarm_on(CONFIG_TIMEOUT);
		res = get_value_java(TRX_JAVA_GATEWAY_REQUEST_INTERNAL, item, result);
		trx_alarm_off();

		if (SUCCEED != res)
		{
			tmp1 = get_rparam(&request, 2);
			/* the default error code "NOTSUPPORTED" renders nodata() trigger function nonfunctional */
			if (NULL != tmp1 && 0 == strcmp(tmp1, "ping"))
				ret = GATEWAY_ERROR;
			goto out;
		}
	}
	else if (0 == strcmp(tmp, "process"))			/* treegix["process",<type>,<mode>,<state>] */
	{
		unsigned char	process_type = TRX_PROCESS_TYPE_UNKNOWN;
		int		process_forks;
		double		value;

		if (2 > nparams || nparams > 4)
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
			goto out;
		}

		process_type = get_process_type_by_name(get_rparam(&request, 1));

		switch (process_type)
		{
			case TRX_PROCESS_TYPE_ALERTMANAGER:
			case TRX_PROCESS_TYPE_ALERTER:
			case TRX_PROCESS_TYPE_ESCALATOR:
			case TRX_PROCESS_TYPE_PROXYPOLLER:
			case TRX_PROCESS_TYPE_TIMER:
				if (0 == (program_type & TRX_PROGRAM_TYPE_SERVER))
					process_type = TRX_PROCESS_TYPE_UNKNOWN;
				break;
			case TRX_PROCESS_TYPE_DATASENDER:
			case TRX_PROCESS_TYPE_HEARTBEAT:
				if (0 == (program_type & TRX_PROGRAM_TYPE_PROXY))
					process_type = TRX_PROCESS_TYPE_UNKNOWN;
				break;
		}

		if (TRX_PROCESS_TYPE_UNKNOWN == process_type)
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
			goto out;
		}

		process_forks = get_process_type_forks(process_type);

		if (NULL == (tmp = get_rparam(&request, 2)))
			tmp = "";

		if (0 == strcmp(tmp, "count"))
		{
			if (4 == nparams)
			{
				SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
				goto out;
			}

			SET_UI64_RESULT(result, process_forks);
		}
		else
		{
			unsigned char	aggr_func, state;
			unsigned short	process_num = 0;

			if ('\0' == *tmp || 0 == strcmp(tmp, "avg"))
				aggr_func = TRX_AGGR_FUNC_AVG;
			else if (0 == strcmp(tmp, "max"))
				aggr_func = TRX_AGGR_FUNC_MAX;
			else if (0 == strcmp(tmp, "min"))
				aggr_func = TRX_AGGR_FUNC_MIN;
			else if (SUCCEED == is_ushort(tmp, &process_num) && 0 < process_num)
				aggr_func = TRX_AGGR_FUNC_ONE;
			else
			{
				SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid third parameter."));
				goto out;
			}

			if (0 == process_forks)
			{
				SET_MSG_RESULT(result, trx_dsprintf(NULL, "No \"%s\" processes started.",
						get_process_type_string(process_type)));
				goto out;
			}
			else if (process_num > process_forks)
			{
				SET_MSG_RESULT(result, trx_dsprintf(NULL, "Process \"%s #%d\" is not started.",
						get_process_type_string(process_type), process_num));
				goto out;
			}

			if (NULL == (tmp = get_rparam(&request, 3)) || '\0' == *tmp || 0 == strcmp(tmp, "busy"))
				state = TRX_PROCESS_STATE_BUSY;
			else if (0 == strcmp(tmp, "idle"))
				state = TRX_PROCESS_STATE_IDLE;
			else
			{
				SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid fourth parameter."));
				goto out;
			}

			get_selfmon_stats(process_type, aggr_func, process_num, state, &value);

			SET_DBL_RESULT(result, value);
		}
	}
	else if (0 == strcmp(tmp, "wcache"))			/* treegix[wcache,<cache>,<mode>] */
	{
		if (2 > nparams || nparams > 3)
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
			goto out;
		}

		tmp = get_rparam(&request, 1);
		tmp1 = get_rparam(&request, 2);

		if (0 == strcmp(tmp, "values"))
		{
			if (NULL == tmp1 || '\0' == *tmp1 || 0 == strcmp(tmp1, "all"))
				SET_UI64_RESULT(result, *(trx_uint64_t *)DCget_stats(TRX_STATS_HISTORY_COUNTER));
			else if (0 == strcmp(tmp1, "float"))
				SET_UI64_RESULT(result, *(trx_uint64_t *)DCget_stats(TRX_STATS_HISTORY_FLOAT_COUNTER));
			else if (0 == strcmp(tmp1, "uint"))
				SET_UI64_RESULT(result, *(trx_uint64_t *)DCget_stats(TRX_STATS_HISTORY_UINT_COUNTER));
			else if (0 == strcmp(tmp1, "str"))
				SET_UI64_RESULT(result, *(trx_uint64_t *)DCget_stats(TRX_STATS_HISTORY_STR_COUNTER));
			else if (0 == strcmp(tmp1, "log"))
				SET_UI64_RESULT(result, *(trx_uint64_t *)DCget_stats(TRX_STATS_HISTORY_LOG_COUNTER));
			else if (0 == strcmp(tmp1, "text"))
				SET_UI64_RESULT(result, *(trx_uint64_t *)DCget_stats(TRX_STATS_HISTORY_TEXT_COUNTER));
			else if (0 == strcmp(tmp1, "not supported"))
				SET_UI64_RESULT(result, *(trx_uint64_t *)DCget_stats(TRX_STATS_NOTSUPPORTED_COUNTER));
			else
			{
				SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid third parameter."));
				goto out;
			}
		}
		else if (0 == strcmp(tmp, "history"))
		{
			if (NULL == tmp1 || '\0' == *tmp1 || 0 == strcmp(tmp1, "pfree"))
				SET_DBL_RESULT(result, *(double *)DCget_stats(TRX_STATS_HISTORY_PFREE));
			else if (0 == strcmp(tmp1, "total"))
				SET_UI64_RESULT(result, *(trx_uint64_t *)DCget_stats(TRX_STATS_HISTORY_TOTAL));
			else if (0 == strcmp(tmp1, "used"))
				SET_UI64_RESULT(result, *(trx_uint64_t *)DCget_stats(TRX_STATS_HISTORY_USED));
			else if (0 == strcmp(tmp1, "free"))
				SET_UI64_RESULT(result, *(trx_uint64_t *)DCget_stats(TRX_STATS_HISTORY_FREE));
			else if (0 == strcmp(tmp1, "pused"))
				SET_DBL_RESULT(result, *(double *)DCget_stats(TRX_STATS_HISTORY_PUSED));
			else
			{
				SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid third parameter."));
				goto out;
			}
		}
		else if (0 == strcmp(tmp, "trend"))
		{
			if (0 == (program_type & TRX_PROGRAM_TYPE_SERVER))
			{
				SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
				goto out;
			}

			if (NULL == tmp1 || '\0' == *tmp1 || 0 == strcmp(tmp1, "pfree"))
				SET_DBL_RESULT(result, *(double *)DCget_stats(TRX_STATS_TREND_PFREE));
			else if (0 == strcmp(tmp1, "total"))
				SET_UI64_RESULT(result, *(trx_uint64_t *)DCget_stats(TRX_STATS_TREND_TOTAL));
			else if (0 == strcmp(tmp1, "used"))
				SET_UI64_RESULT(result, *(trx_uint64_t *)DCget_stats(TRX_STATS_TREND_USED));
			else if (0 == strcmp(tmp1, "free"))
				SET_UI64_RESULT(result, *(trx_uint64_t *)DCget_stats(TRX_STATS_TREND_FREE));
			else if (0 == strcmp(tmp1, "pused"))
				SET_DBL_RESULT(result, *(double *)DCget_stats(TRX_STATS_TREND_PUSED));
			else
			{
				SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid third parameter."));
				goto out;
			}
		}
		else if (0 == strcmp(tmp, "index"))
		{
			if (NULL == tmp1 || '\0' == *tmp1 || 0 == strcmp(tmp1, "pfree"))
				SET_DBL_RESULT(result, *(double *)DCget_stats(TRX_STATS_HISTORY_INDEX_PFREE));
			else if (0 == strcmp(tmp1, "total"))
				SET_UI64_RESULT(result, *(trx_uint64_t *)DCget_stats(TRX_STATS_HISTORY_INDEX_TOTAL));
			else if (0 == strcmp(tmp1, "used"))
				SET_UI64_RESULT(result, *(trx_uint64_t *)DCget_stats(TRX_STATS_HISTORY_INDEX_USED));
			else if (0 == strcmp(tmp1, "free"))
				SET_UI64_RESULT(result, *(trx_uint64_t *)DCget_stats(TRX_STATS_HISTORY_INDEX_FREE));
			else if (0 == strcmp(tmp1, "pused"))
				SET_DBL_RESULT(result, *(double *)DCget_stats(TRX_STATS_HISTORY_INDEX_PUSED));
			else
			{
				SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid third parameter."));
				goto out;
			}
		}
		else
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
			goto out;
		}
	}
	else if (0 == strcmp(tmp, "rcache"))			/* treegix[rcache,<cache>,<mode>] */
	{
		if (2 > nparams || nparams > 3)
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
			goto out;
		}

		tmp = get_rparam(&request, 1);
		tmp1 = get_rparam(&request, 2);

		if (0 == strcmp(tmp, "buffer"))
		{
			if (NULL == tmp1 || '\0' == *tmp1 || 0 == strcmp(tmp1, "pfree"))
				SET_DBL_RESULT(result, *(double *)DCconfig_get_stats(TRX_CONFSTATS_BUFFER_PFREE));
			else if (0 == strcmp(tmp1, "total"))
				SET_UI64_RESULT(result, *(trx_uint64_t *)DCconfig_get_stats(TRX_CONFSTATS_BUFFER_TOTAL));
			else if (0 == strcmp(tmp1, "used"))
				SET_UI64_RESULT(result, *(trx_uint64_t *)DCconfig_get_stats(TRX_CONFSTATS_BUFFER_USED));
			else if (0 == strcmp(tmp1, "free"))
				SET_UI64_RESULT(result, *(trx_uint64_t *)DCconfig_get_stats(TRX_CONFSTATS_BUFFER_FREE));
			else if (0 == strcmp(tmp1, "pused"))
				SET_DBL_RESULT(result, *(double *)DCconfig_get_stats(TRX_CONFSTATS_BUFFER_PUSED));
			else
			{
				SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid third parameter."));
				goto out;
			}
		}
		else
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
			goto out;
		}
	}
	else if (0 == strcmp(tmp, "vmware"))
	{
		trx_vmware_stats_t	stats;

		if (FAIL == trx_vmware_get_statistics(&stats))
		{
			SET_MSG_RESULT(result, trx_dsprintf(NULL, "No \"%s\" processes started.",
					get_process_type_string(TRX_PROCESS_TYPE_VMWARE)));
			goto out;
		}

		if (2 > nparams || nparams > 3)
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
			goto out;
		}

		tmp = get_rparam(&request, 1);
		if (NULL == (tmp1 = get_rparam(&request, 2)))
			tmp1 = "";

		if (0 == strcmp(tmp, "buffer"))
		{
			if (0 == strcmp(tmp1, "free"))
			{
				SET_UI64_RESULT(result, stats.memory_total - stats.memory_used);
			}
			else if (0 == strcmp(tmp1, "pfree"))
			{
				SET_DBL_RESULT(result, (double)(stats.memory_total - stats.memory_used) /
						stats.memory_total * 100);
			}
			else if (0 == strcmp(tmp1, "total"))
			{
				SET_UI64_RESULT(result, stats.memory_total);
			}
			else if (0 == strcmp(tmp1, "used"))
			{
				SET_UI64_RESULT(result, stats.memory_used);
			}
			else if (0 == strcmp(tmp1, "pused"))
			{
				SET_DBL_RESULT(result, (double)stats.memory_used / stats.memory_total * 100);
			}
			else
			{
				SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid third parameter."));
				goto out;
			}
		}
		else
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
			goto out;
		}
	}
	else if (0 == strcmp(tmp, "stats"))			/* treegix[stats,...] */
	{
		const char	*ip_str, *port_str, *ip;
		unsigned short	port_number;
		struct trx_json	json;

		if (6 < nparams)
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
			goto out;
		}

		if (NULL == (ip_str = get_rparam(&request, 1)) || '\0' == *ip_str)
			ip = "127.0.0.1";
		else
			ip = ip_str;

		if (NULL == (port_str = get_rparam(&request, 2)) || '\0' == *port_str)
		{
			port_number = TRX_DEFAULT_SERVER_PORT;
		}
		else if (SUCCEED != is_ushort(port_str, &port_number))
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid third parameter."));
			goto out;
		}

		if (3 >= nparams)
		{
			if ((NULL == ip_str || '\0' == *ip_str) && (NULL == port_str || '\0' == *port_str))
			{
				trx_json_init(&json, TRX_JSON_STAT_BUF_LEN);

				/* Adding "data" object to JSON structure to make identical JSONPath expressions */
				/* work for both data received from internal and external source. */
				trx_json_addobject(&json, TRX_PROTO_TAG_DATA);

				trx_get_treegix_stats(&json);

				trx_json_close(&json);

				set_result_type(result, ITEM_VALUE_TYPE_TEXT, json.buffer);

				trx_json_free(&json);
			}
			else if (SUCCEED != trx_get_remote_treegix_stats(ip, port_number, result))
				goto out;
		}
		else
		{
			tmp1 = get_rparam(&request, 3);

			if (0 == strcmp(tmp1, TRX_PROTO_VALUE_TREEGIX_STATS_QUEUE))
			{
				tmp = get_rparam(&request, 4);		/* from */
				tmp1 = get_rparam(&request, 5);		/* to */

				if ((NULL == ip_str || '\0' == *ip_str) && (NULL == port_str || '\0' == *port_str))
				{
					int	from = TRX_QUEUE_FROM_DEFAULT, to = TRX_QUEUE_TO_INFINITY;

					if (NULL != tmp && '\0' != *tmp &&
							FAIL == is_time_suffix(tmp, &from, TRX_LENGTH_UNLIMITED))
					{
						SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid fifth parameter."));
						goto out;
					}

					if (NULL != tmp1 && '\0' != *tmp1 &&
							FAIL == is_time_suffix(tmp1, &to, TRX_LENGTH_UNLIMITED))
					{
						SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid sixth parameter."));
						goto out;
					}

					if (TRX_QUEUE_TO_INFINITY != to && from > to)
					{
						SET_MSG_RESULT(result, trx_strdup(NULL, "Parameters represent an"
								" invalid interval."));
						goto out;
					}

					trx_json_init(&json, TRX_JSON_STAT_BUF_LEN);

					trx_json_adduint64(&json, TRX_PROTO_VALUE_TREEGIX_STATS_QUEUE,
							DCget_item_queue(NULL, from, to));

					set_result_type(result, ITEM_VALUE_TYPE_TEXT, json.buffer);

					trx_json_free(&json);
				}
				else if (SUCCEED != trx_get_remote_treegix_stats_queue(ip, port_number, tmp, tmp1,
						result))
				{
					goto out;
				}
			}
			else
			{
				SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid forth parameter."));
				goto out;
			}
		}
	}
	else if (0 == strcmp(tmp, "preprocessing_queue"))
	{
		if (1 != nparams)
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
			goto out;
		}

		SET_UI64_RESULT(result, trx_preprocessor_get_queue_size());
	}
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid first parameter."));
		goto out;
	}

	ret = SUCCEED;
out:
	if (NOTSUPPORTED == ret && !ISSET_MSG(result))
		SET_MSG_RESULT(result, trx_strdup(NULL, "Internal check is not supported."));

	free_request(&request);

	return ret;
}
