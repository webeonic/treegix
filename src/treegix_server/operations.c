

#include "common.h"
#include "comms.h"
#include "db.h"
#include "log.h"
#include "dbcache.h"

#include "operations.h"
#include "trxserver.h"

typedef enum
{
	TRX_DISCOVERY_UNSPEC = 0,
	TRX_DISCOVERY_DNS,
	TRX_DISCOVERY_IP,
	TRX_DISCOVERY_VALUE
}
trx_dcheck_source_t;

/******************************************************************************
 *                                                                            *
 * Function: select_discovered_host                                           *
 *                                                                            *
 * Purpose: select hostid of discovered host                                  *
 *                                                                            *
 * Parameters: dhostid - discovered host id                                   *
 *                                                                            *
 * Return value: hostid - existing hostid, 0 - if not found                   *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
static trx_uint64_t	select_discovered_host(const DB_EVENT *event)
{
	DB_RESULT	result;
	DB_ROW		row;
	trx_uint64_t	hostid = 0, proxy_hostid;
	char		*sql = NULL, *ip_esc;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() eventid:" TRX_FS_UI64, __func__, event->eventid);

	switch (event->object)
	{
		case EVENT_OBJECT_DHOST:
		case EVENT_OBJECT_DSERVICE:
			result = DBselect(
					"select dr.proxy_hostid,ds.ip"
					" from drules dr,dchecks dc,dservices ds"
					" where dc.druleid=dr.druleid"
						" and ds.dcheckid=dc.dcheckid"
						" and ds.%s=" TRX_FS_UI64,
					EVENT_OBJECT_DSERVICE == event->object ? "dserviceid" : "dhostid",
					event->objectid);

			if (NULL == (row = DBfetch(result)))
			{
				DBfree_result(result);
				goto exit;
			}

			TRX_DBROW2UINT64(proxy_hostid, row[0]);
			ip_esc = DBdyn_escape_string(row[1]);
			DBfree_result(result);

			sql = trx_dsprintf(sql,
					"select h.hostid"
					" from hosts h,interface i"
					" where h.hostid=i.hostid"
						" and i.ip='%s'"
						" and i.useip=1"
						" and h.status in (%d,%d)"
						" and h.proxy_hostid%s"
					" order by i.hostid",
					ip_esc,
					HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED,
					DBsql_id_cmp(proxy_hostid));

			trx_free(ip_esc);
			break;
		case EVENT_OBJECT_TREEGIX_ACTIVE:
			sql = trx_dsprintf(sql,
					"select h.hostid"
					" from hosts h,autoreg_host a"
					" where h.host=a.host"
						" and a.autoreg_hostid=" TRX_FS_UI64
						" and h.status in (%d,%d)",
					event->objectid,
					HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED);
			break;
		default:
			goto exit;
	}

	result = DBselectN(sql, 1);

	trx_free(sql);

	if (NULL != (row = DBfetch(result)))
		TRX_STR2UINT64(hostid, row[0]);
	DBfree_result(result);
exit:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():" TRX_FS_UI64, __func__, hostid);

	return hostid;
}

/******************************************************************************
 *                                                                            *
 * Function: add_discovered_host_groups                                       *
 *                                                                            *
 * Purpose: add group to host if not added already                            *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
static void	add_discovered_host_groups(trx_uint64_t hostid, trx_vector_uint64_t *groupids)
{
	DB_RESULT	result;
	DB_ROW		row;
	trx_uint64_t	groupid;
	char		*sql = NULL;
	size_t		sql_alloc = 256, sql_offset = 0;
	int		i;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	sql = (char *)trx_malloc(sql, sql_alloc);

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select groupid"
			" from hosts_groups"
			" where hostid=" TRX_FS_UI64
				" and",
			hostid);
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "groupid", groupids->values, groupids->values_num);

	result = DBselect("%s", sql);

	trx_free(sql);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(groupid, row[0]);

		if (FAIL == (i = trx_vector_uint64_search(groupids, groupid, TRX_DEFAULT_UINT64_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		trx_vector_uint64_remove_noorder(groupids, i);
	}
	DBfree_result(result);

	if (0 != groupids->values_num)
	{
		trx_uint64_t	hostgroupid;
		trx_db_insert_t	db_insert;

		hostgroupid = DBget_maxid_num("hosts_groups", groupids->values_num);

		trx_db_insert_prepare(&db_insert, "hosts_groups", "hostgroupid", "hostid", "groupid", NULL);

		trx_vector_uint64_sort(groupids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		for (i = 0; i < groupids->values_num; i++)
		{
			trx_db_insert_add_values(&db_insert, hostgroupid++, hostid, groupids->values[i]);
		}

		trx_db_insert_execute(&db_insert);
		trx_db_insert_clean(&db_insert);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: add_discovered_host                                              *
 *                                                                            *
 * Purpose: add discovered host if it was not added already                   *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value: hostid - new/existing hostid                                 *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
static trx_uint64_t	add_discovered_host(const DB_EVENT *event)
{
	DB_RESULT		result;
	DB_RESULT		result2;
	DB_ROW			row;
	DB_ROW			row2;
	trx_uint64_t		dhostid, hostid = 0, proxy_hostid, druleid;
	char			*host, *host_esc, *host_unique, *host_visible, *host_visible_unique;
	unsigned short		port;
	trx_vector_uint64_t	groupids;
	unsigned char		svc_type, interface_type;
	trx_config_t		cfg;
	trx_db_insert_t		db_insert;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() eventid:" TRX_FS_UI64, __func__, event->eventid);

	trx_vector_uint64_create(&groupids);

	trx_config_get(&cfg, TRX_CONFIG_FLAGS_DISCOVERY_GROUPID | TRX_CONFIG_FLAGS_DEFAULT_INVENTORY_MODE);

	if (TRX_DISCOVERY_GROUPID_UNDEFINED == cfg.discovery_groupid)
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot add discovered host: group for discovered hosts is not defined");
		goto clean;
	}

	trx_vector_uint64_append(&groupids, cfg.discovery_groupid);

	if (EVENT_OBJECT_DHOST == event->object || EVENT_OBJECT_DSERVICE == event->object)
	{
		if (EVENT_OBJECT_DHOST == event->object)
		{
			result = DBselect(
					"select ds.dhostid,dr.proxy_hostid,ds.ip,ds.dns,ds.port,dc.type,"
						"dc.host_source,dc.name_source,dr.druleid"
					" from drules dr,dchecks dc,dservices ds"
					" where dc.druleid=dr.druleid"
						" and ds.dcheckid=dc.dcheckid"
						" and ds.dhostid=" TRX_FS_UI64
					" order by ds.dserviceid",
					event->objectid);
		}
		else
		{
			result = DBselect(
					"select ds.dhostid,dr.proxy_hostid,ds.ip,ds.dns,ds.port,dc.type,"
						"dc.host_source,dc.name_source,dr.druleid"
					" from drules dr,dchecks dc,dservices ds,dservices ds1"
					" where dc.druleid=dr.druleid"
						" and ds.dcheckid=dc.dcheckid"
						" and ds1.dhostid=ds.dhostid"
						" and ds1.dserviceid=" TRX_FS_UI64
					" order by ds.dserviceid",
					event->objectid);
		}

		while (NULL != (row = DBfetch(result)))
		{
			TRX_STR2UINT64(dhostid, row[0]);
			TRX_STR2UINT64(druleid, row[8]);
			TRX_DBROW2UINT64(proxy_hostid, row[1]);
			svc_type = (unsigned char)atoi(row[5]);

			switch (svc_type)
			{
				case SVC_AGENT:
					port = (unsigned short)atoi(row[4]);
					interface_type = INTERFACE_TYPE_AGENT;
					break;
				case SVC_SNMPv1:
				case SVC_SNMPv2c:
				case SVC_SNMPv3:
					port = (unsigned short)atoi(row[4]);
					interface_type = INTERFACE_TYPE_SNMP;
					break;
				default:
					port = TRX_DEFAULT_AGENT_PORT;
					interface_type = INTERFACE_TYPE_AGENT;
			}

			if (0 == hostid)
			{
				result2 = DBselect(
						"select distinct h.hostid"
						" from hosts h,interface i,dservices ds"
						" where h.hostid=i.hostid"
							" and i.ip=ds.ip"
							" and h.status in (%d,%d)"
							" and h.proxy_hostid%s"
							" and ds.dhostid=" TRX_FS_UI64
						" order by h.hostid",
						HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED,
						DBsql_id_cmp(proxy_hostid), dhostid);

				if (NULL != (row2 = DBfetch(result2)))
					TRX_STR2UINT64(hostid, row2[0]);

				DBfree_result(result2);
			}

			if (0 == hostid)
			{
				DB_RESULT		result3;
				DB_ROW			row3;
				trx_dcheck_source_t	host_source, name_source;
				char			*sql = NULL;
				size_t			sql_alloc, sql_offset;

				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
						"select ds.value"
						" from dchecks dc"
							" left join dservices ds"
								" on ds.dcheckid=dc.dcheckid"
									" and ds.dhostid=" TRX_FS_UI64
						" where dc.druleid=" TRX_FS_UI64
							" and dc.host_source=%d"
						" order by ds.dserviceid",
							dhostid, druleid, TRX_DISCOVERY_VALUE);

				result3 = DBselectN(sql, 1);

				if (NULL != (row3 = DBfetch(result3)))
				{
					if (SUCCEED == trx_db_is_null(row3[0]) || '\0' == *row3[0])
					{
						treegix_log(LOG_LEVEL_WARNING, "cannot retrieve service value for"
								" host name on \"%s\"", row[2]);
						host_source = TRX_DISCOVERY_DNS;
					}
					else
						host_source = TRX_DISCOVERY_VALUE;
				}
				else
				{
					if (TRX_DISCOVERY_VALUE == (host_source = atoi(row[6])))
					{
						treegix_log(LOG_LEVEL_WARNING, "cannot retrieve service value for"
								" host name on \"%s\"", row[2]);
						host_source = TRX_DISCOVERY_DNS;
					}
				}

				if (TRX_DISCOVERY_VALUE == host_source)
					host = trx_strdup(NULL, row3[0]);
				else if (TRX_DISCOVERY_IP == host_source || '\0' == *row[3])
					host = trx_strdup(NULL, row[2]);
				else
					host = trx_strdup(NULL, row[3]);

				DBfree_result(result3);

				/* for host uniqueness purposes */
				make_hostname(host);	/* replace not-allowed symbols */
				host_unique = DBget_unique_hostname_by_sample(host, "host");
				trx_free(host);

				sql_offset = 0;
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
						"select ds.value"
						" from dchecks dc"
							" left join dservices ds"
								" on ds.dcheckid=dc.dcheckid"
									" and ds.dhostid=" TRX_FS_UI64
						" where dc.druleid=" TRX_FS_UI64
							" and dc.host_source in (%d,%d,%d,%d)"
							" and dc.name_source=%d"
						" order by ds.dserviceid",
							dhostid, druleid, TRX_DISCOVERY_UNSPEC, TRX_DISCOVERY_DNS,
							TRX_DISCOVERY_IP, TRX_DISCOVERY_VALUE, TRX_DISCOVERY_VALUE);

				result3 = DBselectN(sql, 1);

				if (NULL != (row3 = DBfetch(result3)))
				{
					if (SUCCEED == trx_db_is_null(row3[0]) || '\0' == *row3[0])
					{
						treegix_log(LOG_LEVEL_WARNING, "cannot retrieve service value for"
								" host visible name on \"%s\"", row[2]);
						name_source = TRX_DISCOVERY_UNSPEC;
					}
					else
						name_source = TRX_DISCOVERY_VALUE;
				}
				else
				{
					if (TRX_DISCOVERY_VALUE == (name_source = atoi(row[7])))
					{
						treegix_log(LOG_LEVEL_WARNING, "cannot retrieve service value for"
								" host visible name on \"%s\"", row[2]);
						name_source = TRX_DISCOVERY_UNSPEC;
					}
				}

				if (TRX_DISCOVERY_VALUE == name_source)
					host_visible = trx_strdup(NULL, row3[0]);
				else if (TRX_DISCOVERY_IP == name_source ||
						(TRX_DISCOVERY_DNS == name_source && '\0' == *row[3]))
					host_visible = trx_strdup(NULL, row[2]);
				else if (TRX_DISCOVERY_DNS == name_source)
					host_visible = trx_strdup(NULL, row[3]);
				else
					host_visible = trx_strdup(NULL, host_unique);

				DBfree_result(result3);
				trx_free(sql);

				make_hostname(host_visible);	/* replace not-allowed symbols */
				host_visible_unique = DBget_unique_hostname_by_sample(host_visible, "name");
				trx_free(host_visible);

				hostid = DBget_maxid("hosts");

				trx_db_insert_prepare(&db_insert, "hosts", "hostid", "proxy_hostid", "host", "name",
						NULL);
				trx_db_insert_add_values(&db_insert, hostid, proxy_hostid, host_unique,
						host_visible_unique);
				trx_db_insert_execute(&db_insert);
				trx_db_insert_clean(&db_insert);

				if (HOST_INVENTORY_DISABLED != cfg.default_inventory_mode)
					DBadd_host_inventory(hostid, cfg.default_inventory_mode);

				DBadd_interface(hostid, interface_type, 1, row[2], row[3], port, TRX_CONN_DEFAULT);

				trx_free(host_unique);
				trx_free(host_visible_unique);

				add_discovered_host_groups(hostid, &groupids);
			}
			else
				DBadd_interface(hostid, interface_type, 1, row[2], row[3], port, TRX_CONN_DEFAULT);
		}
		DBfree_result(result);
	}
	else if (EVENT_OBJECT_TREEGIX_ACTIVE == event->object)
	{
		result = DBselect(
				"select proxy_hostid,host,listen_ip,listen_dns,listen_port,flags,tls_accepted"
				" from autoreg_host"
				" where autoreg_hostid=" TRX_FS_UI64,
				event->objectid);

		if (NULL != (row = DBfetch(result)))
		{
			char			*sql = NULL;
			trx_uint64_t		host_proxy_hostid;
			trx_conn_flags_t	flags;
			int			flags_int;
			unsigned char		useip = 1;
			int			tls_accepted;

			TRX_DBROW2UINT64(proxy_hostid, row[0]);
			host_esc = DBdyn_escape_field("hosts", "host", row[1]);
			port = (unsigned short)atoi(row[4]);
			flags_int = atoi(row[5]);

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
							flags_int, row[1]);
			}

			if (TRX_CONN_DNS == flags)
				useip = 0;

			tls_accepted = atoi(row[6]);

			result2 = DBselect(
					"select null"
					" from hosts"
					" where host='%s'"
						" and status=%d",
					host_esc, HOST_STATUS_TEMPLATE);

			if (NULL != (row2 = DBfetch(result2)))
			{
				treegix_log(LOG_LEVEL_WARNING, "cannot add discovered host \"%s\":"
						" template with the same name already exists", row[1]);
				DBfree_result(result2);
				goto out;
			}
			DBfree_result(result2);

			sql = trx_dsprintf(sql,
					"select hostid,proxy_hostid"
					" from hosts"
					" where host='%s'"
						" and flags<>%d"
						" and status in (%d,%d)"
					" order by hostid",
					host_esc, TRX_FLAG_DISCOVERY_PROTOTYPE,
					HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED);

			result2 = DBselectN(sql, 1);

			trx_free(sql);

			if (NULL == (row2 = DBfetch(result2)))
			{
				hostid = DBget_maxid("hosts");

				if (TRX_TCP_SEC_TLS_PSK == tls_accepted)
				{
					char	psk_identity[HOST_TLS_PSK_IDENTITY_LEN_MAX];
					char	psk[HOST_TLS_PSK_LEN_MAX];

					DCget_autoregistration_psk(psk_identity, sizeof(psk_identity),
							(unsigned char *)psk, sizeof(psk));

					trx_db_insert_prepare(&db_insert, "hosts", "hostid", "proxy_hostid",
							"host", "name", "tls_connect", "tls_accept",
							"tls_psk_identity", "tls_psk", NULL);
					trx_db_insert_add_values(&db_insert, hostid, proxy_hostid, row[1], row[1],
						tls_accepted, tls_accepted, psk_identity, psk);
				}
				else
				{
					trx_db_insert_prepare(&db_insert, "hosts", "hostid", "proxy_hostid", "host",
							"name", NULL);
					trx_db_insert_add_values(&db_insert, hostid, proxy_hostid, row[1], row[1]);
				}

				trx_db_insert_execute(&db_insert);
				trx_db_insert_clean(&db_insert);

				if (HOST_INVENTORY_DISABLED != cfg.default_inventory_mode)
					DBadd_host_inventory(hostid, cfg.default_inventory_mode);

				DBadd_interface(hostid, INTERFACE_TYPE_AGENT, useip, row[2], row[3], port, flags);

				add_discovered_host_groups(hostid, &groupids);
			}
			else
			{
				TRX_STR2UINT64(hostid, row2[0]);
				TRX_DBROW2UINT64(host_proxy_hostid, row2[1]);

				if (host_proxy_hostid != proxy_hostid)
				{
					DBexecute("update hosts"
							" set proxy_hostid=%s"
							" where hostid=" TRX_FS_UI64,
							DBsql_id_ins(proxy_hostid), hostid);
				}

				DBadd_interface(hostid, INTERFACE_TYPE_AGENT, useip, row[2], row[3], port, flags);
			}
			DBfree_result(result2);
out:
			trx_free(host_esc);
		}
		DBfree_result(result);
	}
clean:
	trx_config_clean(&cfg);

	trx_vector_uint64_destroy(&groupids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return hostid;
}

/******************************************************************************
 *                                                                            *
 * Function: is_discovery_or_auto_registration                                *
 *                                                                            *
 * Purpose: checks if the event is discovery or auto registration event       *
 *                                                                            *
 * Return value: SUCCEED - it's discovery or auto registration event          *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	is_discovery_or_auto_registration(const DB_EVENT *event)
{
	if (event->source == EVENT_SOURCE_DISCOVERY && (event->object == EVENT_OBJECT_DHOST ||
			event->object == EVENT_OBJECT_DSERVICE))
	{
		return SUCCEED;
	}

	if (event->source == EVENT_SOURCE_AUTO_REGISTRATION && event->object == EVENT_OBJECT_TREEGIX_ACTIVE)
		return SUCCEED;

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: op_host_add                                                      *
 *                                                                            *
 * Purpose: add discovered host                                               *
 *                                                                            *
 * Parameters: trigger - trigger data                                         *
 *             action  - action data                                          *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
void	op_host_add(const DB_EVENT *event)
{
	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (FAIL == is_discovery_or_auto_registration(event))
		return;

	add_discovered_host(event);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: op_host_del                                                      *
 *                                                                            *
 * Purpose: delete host                                                       *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
void	op_host_del(const DB_EVENT *event)
{
	trx_vector_uint64_t	hostids;
	trx_uint64_t		hostid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (FAIL == is_discovery_or_auto_registration(event))
		return;

	if (0 == (hostid = select_discovered_host(event)))
		return;

	trx_vector_uint64_create(&hostids);

	trx_vector_uint64_append(&hostids, hostid);

	DBdelete_hosts_with_prototypes(&hostids);

	trx_vector_uint64_destroy(&hostids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: op_host_enable                                                   *
 *                                                                            *
 * Purpose: enable discovered                                                 *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
void	op_host_enable(const DB_EVENT *event)
{
	trx_uint64_t	hostid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (FAIL == is_discovery_or_auto_registration(event))
		return;

	if (0 == (hostid = add_discovered_host(event)))
		return;

	DBexecute(
			"update hosts"
			" set status=%d"
			" where hostid=" TRX_FS_UI64,
			HOST_STATUS_MONITORED,
			hostid);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: op_host_disable                                                  *
 *                                                                            *
 * Purpose: disable host                                                      *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
void	op_host_disable(const DB_EVENT *event)
{
	trx_uint64_t	hostid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (FAIL == is_discovery_or_auto_registration(event))
		return;

	if (0 == (hostid = add_discovered_host(event)))
		return;

	DBexecute(
			"update hosts"
			" set status=%d"
			" where hostid=" TRX_FS_UI64,
			HOST_STATUS_NOT_MONITORED,
			hostid);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: op_host_inventory_mode                                           *
 *                                                                            *
 * Purpose: sets host inventory mode                                          *
 *                                                                            *
 * Parameters: event          - [IN] the source event                         *
 *             inventory_mode - [IN] the new inventory mode, see              *
 *                              HOST_INVENTORY_ defines                       *
 *                                                                            *
 * Comments: This function does not allow disabling host inventory - only     *
 *           setting manual or automatic host inventory mode is supported.    *
 *                                                                            *
 ******************************************************************************/
void	op_host_inventory_mode(const DB_EVENT *event, int inventory_mode)
{
	trx_uint64_t	hostid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (FAIL == is_discovery_or_auto_registration(event))
		return;

	if (0 == (hostid = add_discovered_host(event)))
		return;

	DBset_host_inventory(hostid, inventory_mode);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: op_groups_add                                                    *
 *                                                                            *
 * Purpose: add groups to discovered host                                     *
 *                                                                            *
 * Parameters: event    - [IN] event data                                     *
 *             groupids - [IN] IDs of groups to add                           *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
void	op_groups_add(const DB_EVENT *event, trx_vector_uint64_t *groupids)
{
	trx_uint64_t	hostid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (FAIL == is_discovery_or_auto_registration(event))
		return;

	if (0 == (hostid = add_discovered_host(event)))
		return;

	add_discovered_host_groups(hostid, groupids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: op_groups_del                                                    *
 *                                                                            *
 * Purpose: delete groups from discovered host                                *
 *                                                                            *
 * Parameters: event    - [IN] event data                                     *
 *             groupids - [IN] IDs of groups to delete                        *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
void	op_groups_del(const DB_EVENT *event, trx_vector_uint64_t *groupids)
{
	DB_RESULT	result;
	trx_uint64_t	hostid;
	char		*sql = NULL;
	size_t		sql_alloc = 256, sql_offset = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (FAIL == is_discovery_or_auto_registration(event))
		return;

	if (0 == (hostid = select_discovered_host(event)))
		return;

	sql = (char *)trx_malloc(sql, sql_alloc);

	/* make sure host belongs to at least one hostgroup */
	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select groupid"
			" from hosts_groups"
			" where hostid=" TRX_FS_UI64
				" and not",
			hostid);
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "groupid", groupids->values, groupids->values_num);

	result = DBselectN(sql, 1);

	if (NULL == DBfetch(result))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot remove host \"%s\" from all host groups:"
				" it must belong to at least one", trx_host_string(hostid));
	}
	else
	{
		sql_offset = 0;
		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"delete from hosts_groups"
				" where hostid=" TRX_FS_UI64
					" and",
				hostid);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "groupid", groupids->values, groupids->values_num);

		DBexecute("%s", sql);
	}
	DBfree_result(result);

	trx_free(sql);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: op_template_add                                                  *
 *                                                                            *
 * Purpose: link host with template                                           *
 *                                                                            *
 * Parameters: event           - [IN] event data                              *
 *             lnk_templateids - [IN] array of template IDs                   *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
void	op_template_add(const DB_EVENT *event, trx_vector_uint64_t *lnk_templateids)
{
	trx_uint64_t	hostid;
	char		*error;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (FAIL == is_discovery_or_auto_registration(event))
		return;

	if (0 == (hostid = add_discovered_host(event)))
		return;

	if (SUCCEED != DBcopy_template_elements(hostid, lnk_templateids, &error))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot link template(s) %s", error);
		trx_free(error);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: op_template_del                                                  *
 *                                                                            *
 * Purpose: unlink and clear host from template                               *
 *                                                                            *
 * Parameters: event           - [IN] event data                              *
 *             lnk_templateids - [IN] array of template IDs                   *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
void	op_template_del(const DB_EVENT *event, trx_vector_uint64_t *del_templateids)
{
	trx_uint64_t	hostid;
	char		*error;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (FAIL == is_discovery_or_auto_registration(event))
		return;

	if (0 == (hostid = select_discovered_host(event)))
		return;

	if (SUCCEED != DBdelete_template_elements(hostid, del_templateids, &error))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot unlink template: %s", error);
		trx_free(error);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}
