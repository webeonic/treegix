

#include "common.h"

#include "db.h"
#include "log.h"
#include "sysinfo.h"
#include "trxicmpping.h"
#include "discovery.h"
#include "trxserver.h"
#include "trxself.h"

#include "daemon.h"
#include "discoverer.h"
#include "../poller/checks_agent.h"
#include "../poller/checks_snmp.h"
#include "../../libs/trxcrypto/tls.h"

extern int		CONFIG_DISCOVERER_FORKS;
extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

#define TRX_DISCOVERER_IPRANGE_LIMIT	(1 << 16)

/******************************************************************************
 *                                                                            *
 * Function: proxy_update_service                                             *
 *                                                                            *
 * Purpose: process new service status                                        *
 *                                                                            *
 * Parameters: service - service info                                         *
 *                                                                            *
 ******************************************************************************/
static void	proxy_update_service(trx_uint64_t druleid, trx_uint64_t dcheckid, const char *ip,
		const char *dns, int port, int status, const char *value, int now)
{
	char	*ip_esc, *dns_esc, *value_esc;

	ip_esc = DBdyn_escape_field("proxy_dhistory", "ip", ip);
	dns_esc = DBdyn_escape_field("proxy_dhistory", "dns", dns);
	value_esc = DBdyn_escape_field("proxy_dhistory", "value", value);

	DBexecute("insert into proxy_dhistory (clock,druleid,dcheckid,ip,dns,port,value,status)"
			" values (%d," TRX_FS_UI64 "," TRX_FS_UI64 ",'%s','%s',%d,'%s',%d)",
			now, druleid, dcheckid, ip_esc, dns_esc, port, value_esc, status);

	trx_free(value_esc);
	trx_free(dns_esc);
	trx_free(ip_esc);
}

/******************************************************************************
 *                                                                            *
 * Function: proxy_update_host                                                *
 *                                                                            *
 * Purpose: process new service status                                        *
 *                                                                            *
 * Parameters: service - service info                                         *
 *                                                                            *
 ******************************************************************************/
static void	proxy_update_host(trx_uint64_t druleid, const char *ip, const char *dns, int status, int now)
{
	char	*ip_esc, *dns_esc;

	ip_esc = DBdyn_escape_field("proxy_dhistory", "ip", ip);
	dns_esc = DBdyn_escape_field("proxy_dhistory", "dns", dns);

	DBexecute("insert into proxy_dhistory (clock,druleid,ip,dns,status)"
			" values (%d," TRX_FS_UI64 ",'%s','%s',%d)",
			now, druleid, ip_esc, dns_esc, status);

	trx_free(dns_esc);
	trx_free(ip_esc);
}

/******************************************************************************
 *                                                                            *
 * Function: discover_service                                                 *
 *                                                                            *
 * Purpose: check if service is available                                     *
 *                                                                            *
 * Parameters: service type, ip address, port number                          *
 *                                                                            *
 * Return value: SUCCEED - service is UP, FAIL - service not discovered       *
 *                                                                            *
 ******************************************************************************/
static int	discover_service(const DB_DCHECK *dcheck, char *ip, int port, char **value, size_t *value_alloc)
{
	int		ret = SUCCEED;
	const char	*service = NULL;
	AGENT_RESULT 	result;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	init_result(&result);

	**value = '\0';

	switch (dcheck->type)
	{
		case SVC_SSH:
			service = "ssh";
			break;
		case SVC_LDAP:
			service = "ldap";
			break;
		case SVC_SMTP:
			service = "smtp";
			break;
		case SVC_FTP:
			service = "ftp";
			break;
		case SVC_HTTP:
			service = "http";
			break;
		case SVC_POP:
			service = "pop";
			break;
		case SVC_NNTP:
			service = "nntp";
			break;
		case SVC_IMAP:
			service = "imap";
			break;
		case SVC_TCP:
			service = "tcp";
			break;
		case SVC_HTTPS:
			service = "https";
			break;
		case SVC_TELNET:
			service = "telnet";
			break;
		case SVC_AGENT:
		case SVC_SNMPv1:
		case SVC_SNMPv2c:
		case SVC_SNMPv3:
		case SVC_ICMPPING:
			break;
		default:
			ret = FAIL;
			break;
	}

	if (SUCCEED == ret)
	{
		char		**pvalue;
		size_t		value_offset = 0;
		TRX_FPING_HOST	host;
		DC_ITEM		item;
		char		key[MAX_STRING_LEN], error[ITEM_ERROR_LEN_MAX];

		trx_alarm_on(CONFIG_TIMEOUT);

		switch (dcheck->type)
		{
			/* simple checks */
			case SVC_SSH:
			case SVC_LDAP:
			case SVC_SMTP:
			case SVC_FTP:
			case SVC_HTTP:
			case SVC_POP:
			case SVC_NNTP:
			case SVC_IMAP:
			case SVC_TCP:
			case SVC_HTTPS:
			case SVC_TELNET:
				trx_snprintf(key, sizeof(key), "net.tcp.service[%s,%s,%d]", service, ip, port);

				if (SUCCEED != process(key, 0, &result) || NULL == GET_UI64_RESULT(&result) ||
						0 == result.ui64)
				{
					ret = FAIL;
				}
				break;
			/* agent and SNMP checks */
			case SVC_AGENT:
			case SVC_SNMPv1:
			case SVC_SNMPv2c:
			case SVC_SNMPv3:
				memset(&item, 0, sizeof(DC_ITEM));

				strscpy(item.key_orig, dcheck->key_);
				item.key = item.key_orig;

				item.interface.useip = 1;
				item.interface.addr = ip;
				item.interface.port = port;

				item.value_type	= ITEM_VALUE_TYPE_STR;

				switch (dcheck->type)
				{
					case SVC_SNMPv1:
						item.type = ITEM_TYPE_SNMPv1;
						break;
					case SVC_SNMPv2c:
						item.type = ITEM_TYPE_SNMPv2c;
						break;
					case SVC_SNMPv3:
						item.type = ITEM_TYPE_SNMPv3;
						break;
					default:
						item.type = ITEM_TYPE_TREEGIX;
						break;
				}

				if (SVC_AGENT == dcheck->type)
				{
					item.host.tls_connect = TRX_TCP_SEC_UNENCRYPTED;

					if (SUCCEED == get_value_agent(&item, &result) &&
							NULL != (pvalue = GET_TEXT_RESULT(&result)))
					{
						trx_strcpy_alloc(value, value_alloc, &value_offset, *pvalue);
					}
					else
						ret = FAIL;
				}
				else
#ifdef HAVE_NETSNMP
				{
					item.snmp_community = strdup(dcheck->snmp_community);
					item.snmp_oid = strdup(dcheck->key_);

					substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
							&item.snmp_community, MACRO_TYPE_COMMON, NULL, 0);
					substitute_key_macros(&item.snmp_oid, NULL, NULL, NULL, NULL,
							MACRO_TYPE_SNMP_OID, NULL, 0);

					if (ITEM_TYPE_SNMPv3 == item.type)
					{
						item.snmpv3_securityname =
								trx_strdup(NULL, dcheck->snmpv3_securityname);
						item.snmpv3_securitylevel = dcheck->snmpv3_securitylevel;
						item.snmpv3_authpassphrase =
								trx_strdup(NULL, dcheck->snmpv3_authpassphrase);
						item.snmpv3_privpassphrase =
								trx_strdup(NULL, dcheck->snmpv3_privpassphrase);
						item.snmpv3_authprotocol = dcheck->snmpv3_authprotocol;
						item.snmpv3_privprotocol = dcheck->snmpv3_privprotocol;
						item.snmpv3_contextname = trx_strdup(NULL, dcheck->snmpv3_contextname);

						substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
								NULL, &item.snmpv3_securityname, MACRO_TYPE_COMMON,
								NULL, 0);
						substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
								NULL, &item.snmpv3_authpassphrase, MACRO_TYPE_COMMON,
								NULL, 0);
						substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
								NULL, &item.snmpv3_privpassphrase, MACRO_TYPE_COMMON,
								NULL, 0);
						substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
								NULL, &item.snmpv3_contextname, MACRO_TYPE_COMMON,
								NULL, 0);
					}

					if (SUCCEED == get_value_snmp(&item, &result) &&
							NULL != (pvalue = GET_TEXT_RESULT(&result)))
					{
						trx_strcpy_alloc(value, value_alloc, &value_offset, *pvalue);
					}
					else
						ret = FAIL;

					trx_free(item.snmp_community);
					trx_free(item.snmp_oid);

					if (ITEM_TYPE_SNMPv3 == item.type)
					{
						trx_free(item.snmpv3_securityname);
						trx_free(item.snmpv3_authpassphrase);
						trx_free(item.snmpv3_privpassphrase);
						trx_free(item.snmpv3_contextname);
					}
				}
#else
					ret = FAIL;
#endif	/* HAVE_NETSNMP */

				if (FAIL == ret && ISSET_MSG(&result))
				{
					treegix_log(LOG_LEVEL_DEBUG, "discovery: item [%s] error: %s",
							item.key, result.msg);
				}
				break;
			case SVC_ICMPPING:
				memset(&host, 0, sizeof(host));
				host.addr = strdup(ip);

				if (SUCCEED != do_ping(&host, 1, 3, 0, 0, 0, error, sizeof(error)) || 0 == host.rcv)
					ret = FAIL;

				trx_free(host.addr);
				break;
			default:
				break;
		}

		trx_alarm_off();
	}
	free_result(&result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_check                                                    *
 *                                                                            *
 * Purpose: check if service is available and update database                 *
 *                                                                            *
 * Parameters: service - service info                                         *
 *                                                                            *
 ******************************************************************************/
static void	process_check(const DB_DCHECK *dcheck, int *host_status, char *ip, int now, trx_vector_ptr_t *services)
{
	const char	*start;
	char		*value = NULL;
	size_t		value_alloc = 128;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	value = (char *)trx_malloc(value, value_alloc);

	for (start = dcheck->ports; '\0' != *start;)
	{
		char	*comma, *last_port;
		int	port, first, last;

		if (NULL != (comma = strchr(start, ',')))
			*comma = '\0';

		if (NULL != (last_port = strchr(start, '-')))
		{
			*last_port = '\0';
			first = atoi(start);
			last = atoi(last_port + 1);
			*last_port = '-';
		}
		else
			first = last = atoi(start);

		for (port = first; port <= last; port++)
		{
			trx_service_t	*service;

			treegix_log(LOG_LEVEL_DEBUG, "%s() port:%d", __func__, port);

			service = (trx_service_t *)trx_malloc(NULL, sizeof(trx_service_t));
			service->status = (SUCCEED == discover_service(dcheck, ip, port, &value, &value_alloc) ?
					DOBJECT_STATUS_UP : DOBJECT_STATUS_DOWN);
			service->dcheckid = dcheck->dcheckid;
			service->itemtime = (time_t)now;
			service->port = port;
			trx_strlcpy_utf8(service->value, value, MAX_DISCOVERED_VALUE_SIZE);
			trx_vector_ptr_append(services, service);

			/* update host status */
			if (-1 == *host_status || DOBJECT_STATUS_UP == service->status)
				*host_status = service->status;
		}

		if (NULL != comma)
		{
			*comma = ',';
			start = comma + 1;
		}
		else
			break;
	}
	trx_free(value);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: process_checks                                                   *
 *                                                                            *
 ******************************************************************************/
static void	process_checks(const DB_DRULE *drule, int *host_status, char *ip, int unique, int now,
		trx_vector_ptr_t *services, trx_vector_uint64_t *dcheckids)
{
	DB_RESULT	result;
	DB_ROW		row;
	DB_DCHECK	dcheck;
	char		sql[MAX_STRING_LEN];
	size_t		offset = 0;

	offset += trx_snprintf(sql + offset, sizeof(sql) - offset,
			"select dcheckid,type,key_,snmp_community,snmpv3_securityname,snmpv3_securitylevel,"
				"snmpv3_authpassphrase,snmpv3_privpassphrase,snmpv3_authprotocol,snmpv3_privprotocol,"
				"ports,snmpv3_contextname"
			" from dchecks"
			" where druleid=" TRX_FS_UI64,
			drule->druleid);

	if (0 != drule->unique_dcheckid)
	{
		offset += trx_snprintf(sql + offset, sizeof(sql) - offset, " and dcheckid%s" TRX_FS_UI64,
				unique ? "=" : "<>", drule->unique_dcheckid);
	}

	trx_snprintf(sql + offset, sizeof(sql) - offset, " order by dcheckid");

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		memset(&dcheck, 0, sizeof(dcheck));

		TRX_STR2UINT64(dcheck.dcheckid, row[0]);
		dcheck.type = atoi(row[1]);
		dcheck.key_ = row[2];
		dcheck.snmp_community = row[3];
		dcheck.snmpv3_securityname = row[4];
		dcheck.snmpv3_securitylevel = (unsigned char)atoi(row[5]);
		dcheck.snmpv3_authpassphrase = row[6];
		dcheck.snmpv3_privpassphrase = row[7];
		dcheck.snmpv3_authprotocol = (unsigned char)atoi(row[8]);
		dcheck.snmpv3_privprotocol = (unsigned char)atoi(row[9]);
		dcheck.ports = row[10];
		dcheck.snmpv3_contextname = row[11];

		trx_vector_uint64_append(dcheckids, dcheck.dcheckid);

		process_check(&dcheck, host_status, ip, now, services);
	}
	DBfree_result(result);
}

/******************************************************************************
 *                                                                            *
 * Function: process_services                                                 *
 *                                                                            *
 ******************************************************************************/
static int	process_services(const DB_DRULE *drule, DB_DHOST *dhost, const char *ip, const char *dns, int now,
		const trx_vector_ptr_t *services, trx_vector_uint64_t *dcheckids)
{
	int	i, ret;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_sort(dcheckids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	if (SUCCEED != (ret = DBlock_ids("dchecks", "dcheckid", dcheckids)))
		goto fail;

	for (i = 0; i < services->values_num; i++)
	{
		trx_service_t	*service = (trx_service_t *)services->values[i];

		if (FAIL == trx_vector_uint64_bsearch(dcheckids, service->dcheckid, TRX_DEFAULT_UINT64_COMPARE_FUNC))
			continue;

		if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
		{
			discovery_update_service(drule, service->dcheckid, dhost, ip, dns, service->port,
					service->status, service->value, now);
		}
		else if (0 != (program_type & TRX_PROGRAM_TYPE_PROXY))
		{
			proxy_update_service(drule->druleid, service->dcheckid, ip, dns, service->port,
					service->status, service->value, now);
		}
	}
fail:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_rule                                                     *
 *                                                                            *
 * Purpose: process single discovery rule                                     *
 *                                                                            *
 ******************************************************************************/
static void	process_rule(DB_DRULE *drule)
{
	DB_DHOST		dhost;
	int			host_status, now;
	char			ip[INTERFACE_IP_LEN_MAX], *start, *comma, dns[INTERFACE_DNS_LEN_MAX];
	int			ipaddress[8];
	trx_iprange_t		iprange;
	trx_vector_ptr_t	services;
	trx_vector_uint64_t	dcheckids;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() rule:'%s' range:'%s'", __func__, drule->name, drule->iprange);

	trx_vector_ptr_create(&services);
	trx_vector_uint64_create(&dcheckids);

	for (start = drule->iprange; '\0' != *start;)
	{
		if (NULL != (comma = strchr(start, ',')))
			*comma = '\0';

		treegix_log(LOG_LEVEL_DEBUG, "%s() range:'%s'", __func__, start);

		if (SUCCEED != iprange_parse(&iprange, start))
		{
			treegix_log(LOG_LEVEL_WARNING, "discovery rule \"%s\": wrong format of IP range \"%s\"",
					drule->name, start);
			goto next;
		}

		if (TRX_DISCOVERER_IPRANGE_LIMIT < iprange_volume(&iprange))
		{
			treegix_log(LOG_LEVEL_WARNING, "discovery rule \"%s\": IP range \"%s\" exceeds %d address limit",
					drule->name, start, TRX_DISCOVERER_IPRANGE_LIMIT);
			goto next;
		}
#ifndef HAVE_IPV6
		if (TRX_IPRANGE_V6 == iprange.type)
		{
			treegix_log(LOG_LEVEL_WARNING, "discovery rule \"%s\": encountered IP range \"%s\","
					" but IPv6 support not compiled in", drule->name, start);
			goto next;
		}
#endif
		iprange_first(&iprange, ipaddress);

		do
		{
#ifdef HAVE_IPV6
			if (TRX_IPRANGE_V6 == iprange.type)
			{
				trx_snprintf(ip, sizeof(ip), "%x:%x:%x:%x:%x:%x:%x:%x", (unsigned int)ipaddress[0],
						(unsigned int)ipaddress[1], (unsigned int)ipaddress[2],
						(unsigned int)ipaddress[3], (unsigned int)ipaddress[4],
						(unsigned int)ipaddress[5], (unsigned int)ipaddress[6],
						(unsigned int)ipaddress[7]);
			}
			else
			{
#endif
				trx_snprintf(ip, sizeof(ip), "%u.%u.%u.%u", (unsigned int)ipaddress[0],
						(unsigned int)ipaddress[1], (unsigned int)ipaddress[2],
						(unsigned int)ipaddress[3]);
#ifdef HAVE_IPV6
			}
#endif
			memset(&dhost, 0, sizeof(dhost));
			host_status = -1;

			now = time(NULL);

			treegix_log(LOG_LEVEL_DEBUG, "%s() ip:'%s'", __func__, ip);

			trx_alarm_on(CONFIG_TIMEOUT);
			trx_gethost_by_ip(ip, dns, sizeof(dns));
			trx_alarm_off();

			if (0 != drule->unique_dcheckid)
				process_checks(drule, &host_status, ip, 1, now, &services, &dcheckids);
			process_checks(drule, &host_status, ip, 0, now, &services, &dcheckids);

			DBbegin();

			if (SUCCEED != DBlock_druleid(drule->druleid))
			{
				DBrollback();

				treegix_log(LOG_LEVEL_DEBUG, "discovery rule '%s' was deleted during processing,"
						" stopping", drule->name);
				trx_vector_ptr_clear_ext(&services, trx_ptr_free);
				goto out;
			}

			if (SUCCEED != process_services(drule, &dhost, ip, dns, now, &services, &dcheckids))
			{
				DBrollback();

				treegix_log(LOG_LEVEL_DEBUG, "all checks where deleted for discovery rule '%s'"
						" during processing, stopping", drule->name);
				trx_vector_ptr_clear_ext(&services, trx_ptr_free);
				goto out;
			}

			trx_vector_uint64_clear(&dcheckids);
			trx_vector_ptr_clear_ext(&services, trx_ptr_free);

			if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
				discovery_update_host(&dhost, host_status, now);
			else if (0 != (program_type & TRX_PROGRAM_TYPE_PROXY))
				proxy_update_host(drule->druleid, ip, dns, host_status, now);

			DBcommit();
		}
		while (SUCCEED == iprange_next(&iprange, ipaddress));
next:
		if (NULL != comma)
		{
			*comma = ',';
			start = comma + 1;
		}
		else
			break;
	}
out:
	trx_vector_ptr_destroy(&services);
	trx_vector_uint64_destroy(&dcheckids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: discovery_clean_services                                         *
 *                                                                            *
 * Purpose: clean dservices and dhosts not presenting in drule                *
 *                                                                            *
 ******************************************************************************/
static void	discovery_clean_services(trx_uint64_t druleid)
{
	DB_RESULT		result;
	DB_ROW			row;
	char			*iprange = NULL;
	trx_vector_uint64_t	keep_dhostids, del_dhostids, del_dserviceids;
	trx_uint64_t		dhostid, dserviceid;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	result = DBselect("select iprange from drules where druleid=" TRX_FS_UI64, druleid);

	if (NULL != (row = DBfetch(result)))
		iprange = trx_strdup(iprange, row[0]);

	DBfree_result(result);

	if (NULL == iprange)
		goto out;

	trx_vector_uint64_create(&keep_dhostids);
	trx_vector_uint64_create(&del_dhostids);
	trx_vector_uint64_create(&del_dserviceids);

	result = DBselect(
			"select dh.dhostid,ds.dserviceid,ds.ip"
			" from dhosts dh"
				" left join dservices ds"
					" on dh.dhostid=ds.dhostid"
			" where dh.druleid=" TRX_FS_UI64,
			druleid);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(dhostid, row[0]);

		if (SUCCEED == DBis_null(row[1]))
		{
			trx_vector_uint64_append(&del_dhostids, dhostid);
		}
		else if (SUCCEED != ip_in_list(iprange, row[2]))
		{
			TRX_STR2UINT64(dserviceid, row[1]);

			trx_vector_uint64_append(&del_dhostids, dhostid);
			trx_vector_uint64_append(&del_dserviceids, dserviceid);
		}
		else
			trx_vector_uint64_append(&keep_dhostids, dhostid);
	}
	DBfree_result(result);

	trx_free(iprange);

	if (0 != del_dserviceids.values_num)
	{
		int	i;

		/* remove dservices */

		trx_vector_uint64_sort(&del_dserviceids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		sql_offset = 0;
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from dservices where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "dserviceid",
				del_dserviceids.values, del_dserviceids.values_num);

		DBexecute("%s", sql);

		/* remove dhosts */

		trx_vector_uint64_sort(&keep_dhostids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
		trx_vector_uint64_uniq(&keep_dhostids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		trx_vector_uint64_sort(&del_dhostids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
		trx_vector_uint64_uniq(&del_dhostids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		for (i = 0; i < del_dhostids.values_num; i++)
		{
			dhostid = del_dhostids.values[i];

			if (FAIL != trx_vector_uint64_bsearch(&keep_dhostids, dhostid, TRX_DEFAULT_UINT64_COMPARE_FUNC))
				trx_vector_uint64_remove_noorder(&del_dhostids, i--);
		}
	}

	if (0 != del_dhostids.values_num)
	{
		trx_vector_uint64_sort(&del_dhostids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		sql_offset = 0;
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from dhosts where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "dhostid",
				del_dhostids.values, del_dhostids.values_num);

		DBexecute("%s", sql);
	}

	trx_free(sql);

	trx_vector_uint64_destroy(&del_dserviceids);
	trx_vector_uint64_destroy(&del_dhostids);
	trx_vector_uint64_destroy(&keep_dhostids);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static int	process_discovery(void)
{
	DB_RESULT	result;
	DB_ROW		row;
	int		rule_count = 0;
	char		*delay_str = NULL;

	result = DBselect(
			"select distinct r.druleid,r.iprange,r.name,c.dcheckid,r.proxy_hostid,r.delay"
			" from drules r"
				" left join dchecks c"
					" on c.druleid=r.druleid"
						" and c.uniq=1"
			" where r.status=%d"
				" and r.nextcheck<=%d"
				" and " TRX_SQL_MOD(r.druleid,%d) "=%d",
			DRULE_STATUS_MONITORED,
			(int)time(NULL),
			CONFIG_DISCOVERER_FORKS,
			process_num - 1);

	while (TRX_IS_RUNNING() && NULL != (row = DBfetch(result)))
	{
		int		now, delay;
		trx_uint64_t	druleid;

		rule_count++;

		TRX_STR2UINT64(druleid, row[0]);

		delay_str = trx_strdup(delay_str, row[5]);
		substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &delay_str,
				MACRO_TYPE_COMMON, NULL, 0);

		if (SUCCEED != is_time_suffix(delay_str, &delay, TRX_LENGTH_UNLIMITED))
		{
			trx_config_t	cfg;

			treegix_log(LOG_LEVEL_WARNING, "discovery rule \"%s\": invalid update interval \"%s\"",
					row[2], delay_str);

			trx_config_get(&cfg, TRX_CONFIG_FLAGS_REFRESH_UNSUPPORTED);

			now = (int)time(NULL);

			DBexecute("update drules set nextcheck=%d where druleid=" TRX_FS_UI64,
					(0 == cfg.refresh_unsupported || 0 > now + cfg.refresh_unsupported ?
					TRX_JAN_2038 : now + cfg.refresh_unsupported), druleid);

			trx_config_clean(&cfg);
			continue;
		}

		if (SUCCEED == DBis_null(row[4]))
		{
			DB_DRULE	drule;

			memset(&drule, 0, sizeof(drule));

			drule.druleid = druleid;
			drule.iprange = row[1];
			drule.name = row[2];
			TRX_DBROW2UINT64(drule.unique_dcheckid, row[3]);

			process_rule(&drule);
		}

		if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
			discovery_clean_services(druleid);

		now = (int)time(NULL);
		if (0 > now + delay)
		{
			treegix_log(LOG_LEVEL_WARNING, "discovery rule \"%s\": nextcheck update causes overflow",
					row[2]);
			DBexecute("update drules set nextcheck=%d where druleid=" TRX_FS_UI64, TRX_JAN_2038, druleid);
		}
		else
			DBexecute("update drules set nextcheck=%d where druleid=" TRX_FS_UI64, now + delay, druleid);
	}
	DBfree_result(result);

	trx_free(delay_str);

	return rule_count;	/* performance metric */
}

static int	get_minnextcheck(void)
{
	DB_RESULT	result;
	DB_ROW		row;
	int		res = FAIL;

	result = DBselect(
			"select count(*),min(nextcheck)"
			" from drules"
			" where status=%d"
				" and " TRX_SQL_MOD(druleid,%d) "=%d",
			DRULE_STATUS_MONITORED, CONFIG_DISCOVERER_FORKS, process_num - 1);

	row = DBfetch(result);

	if (NULL == row || DBis_null(row[0]) == SUCCEED || DBis_null(row[1]) == SUCCEED)
		treegix_log(LOG_LEVEL_DEBUG, "get_minnextcheck(): no items to update");
	else if (0 != atoi(row[0]))
		res = atoi(row[1]);

	DBfree_result(result);

	return res;
}

/******************************************************************************
 *                                                                            *
 * Function: discoverer_thread                                                *
 *                                                                            *
 * Purpose: periodically try to find new hosts and services                   *
 *                                                                            *
 ******************************************************************************/
TRX_THREAD_ENTRY(discoverer_thread, args)
{
	int	nextcheck, sleeptime = -1, rule_count = 0, old_rule_count = 0;
	double	sec, total_sec = 0.0, old_total_sec = 0.0;
	time_t	last_stat_time;

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

#ifdef HAVE_NETSNMP
	trx_init_snmp();
#endif

#define STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

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
			trx_setproctitle("%s #%d [processed %d rules in " TRX_FS_DBL " sec, performing discovery]",
					get_process_type_string(process_type), process_num, old_rule_count,
					old_total_sec);
		}

		rule_count += process_discovery();
		total_sec += trx_time() - sec;

		nextcheck = get_minnextcheck();
		sleeptime = calculate_sleeptime(nextcheck, DISCOVERER_DELAY);

		if (0 != sleeptime || STAT_INTERVAL <= time(NULL) - last_stat_time)
		{
			if (0 == sleeptime)
			{
				trx_setproctitle("%s #%d [processed %d rules in " TRX_FS_DBL " sec, performing "
						"discovery]", get_process_type_string(process_type), process_num,
						rule_count, total_sec);
			}
			else
			{
				trx_setproctitle("%s #%d [processed %d rules in " TRX_FS_DBL " sec, idle %d sec]",
						get_process_type_string(process_type), process_num, rule_count,
						total_sec, sleeptime);
				old_rule_count = rule_count;
				old_total_sec = total_sec;
			}
			rule_count = 0;
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
