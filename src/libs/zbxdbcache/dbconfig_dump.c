
#include "common.h"
#include "log.h"
#include "trxalgo.h"
#include "dbcache.h"
#include "mutexs.h"

#define TRX_DBCONFIG_IMPL
#include "dbconfig.h"

static void	DCdump_config(void)
{
	int	i;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	if (NULL == config->config)
		goto out;

	treegix_log(LOG_LEVEL_TRACE, "refresh_unsupported:%d", config->config->refresh_unsupported);
	treegix_log(LOG_LEVEL_TRACE, "discovery_groupid:" TRX_FS_UI64, config->config->discovery_groupid);
	treegix_log(LOG_LEVEL_TRACE, "snmptrap_logging:%hhu", config->config->snmptrap_logging);
	treegix_log(LOG_LEVEL_TRACE, "default_inventory_mode:%d", config->config->default_inventory_mode);
	treegix_log(LOG_LEVEL_TRACE, "db_extension: %s", config->config->db_extension);
	treegix_log(LOG_LEVEL_TRACE, "autoreg_tls_accept:%hhu", config->config->autoreg_tls_accept);

	treegix_log(LOG_LEVEL_TRACE, "severity names:");
	for (i = 0; TRIGGER_SEVERITY_COUNT > i; i++)
		treegix_log(LOG_LEVEL_TRACE, "  %s", config->config->severity_name[i]);

	treegix_log(LOG_LEVEL_TRACE, "housekeeping:");
	treegix_log(LOG_LEVEL_TRACE, "  events, mode:%u period:[trigger:%d internal:%d autoreg:%d discovery:%d]",
			config->config->hk.events_mode, config->config->hk.events_trigger,
			config->config->hk.events_internal, config->config->hk.events_autoreg,
			config->config->hk.events_discovery);

	treegix_log(LOG_LEVEL_TRACE, "  audit, mode:%u period:%d", config->config->hk.audit_mode,
			config->config->hk.audit);

	treegix_log(LOG_LEVEL_TRACE, "  it services, mode:%u period:%d", config->config->hk.services_mode,
			config->config->hk.services);

	treegix_log(LOG_LEVEL_TRACE, "  user sessions, mode:%u period:%d", config->config->hk.sessions_mode,
			config->config->hk.sessions);

	treegix_log(LOG_LEVEL_TRACE, "  history, mode:%u global:%u period:%d", config->config->hk.history_mode,
			config->config->hk.history_global, config->config->hk.history);

	treegix_log(LOG_LEVEL_TRACE, "  trends, mode:%u global:%u period:%d", config->config->hk.trends_mode,
			config->config->hk.trends_global, config->config->hk.trends);

out:
	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_hosts(void)
{
	TRX_DC_HOST		*host;
	trx_hashset_iter_t	iter;
	trx_vector_ptr_t	index;
	int			i;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->hosts, &iter);

	while (NULL != (host = (TRX_DC_HOST *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, host);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		int	j;

		host = (TRX_DC_HOST *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "hostid:" TRX_FS_UI64 " host:'%s' name:'%s' status:%u", host->hostid,
				host->host, host->name, host->status);

		treegix_log(LOG_LEVEL_TRACE, "  proxy_hostid:" TRX_FS_UI64, host->proxy_hostid);
		treegix_log(LOG_LEVEL_TRACE, "  data_expected_from:%d", host->data_expected_from);

		treegix_log(LOG_LEVEL_TRACE, "  treegix:[available:%u, errors_from:%d disable_until:%d error:'%s']",
				host->available, host->errors_from, host->disable_until, host->error);
		treegix_log(LOG_LEVEL_TRACE, "  snmp:[available:%u, errors_from:%d disable_until:%d error:'%s']",
				host->snmp_available, host->snmp_errors_from, host->snmp_disable_until,
				host->snmp_error);
		treegix_log(LOG_LEVEL_TRACE, "  ipmi:[available:%u, errors_from:%d disable_until:%d error:'%s']",
				host->ipmi_available, host->ipmi_errors_from, host->ipmi_disable_until,
				host->ipmi_error);
		treegix_log(LOG_LEVEL_TRACE, "  jmx:[available:%u, errors_from:%d disable_until:%d error:'%s']",
				host->jmx_available, host->jmx_errors_from, host->jmx_disable_until, host->jmx_error);

		/* timestamp of last availability status (available/error) field change on any interface */
		treegix_log(LOG_LEVEL_TRACE, "  availability_ts:%d", host->availability_ts);

		treegix_log(LOG_LEVEL_TRACE, "  maintenanceid:" TRX_FS_UI64 " maintenance_status:%u maintenance_type:%u"
				" maintenance_from:%d", host->maintenanceid, host->maintenance_status,
				host->maintenance_type, host->maintenance_from);

		treegix_log(LOG_LEVEL_TRACE, "  number of items: treegix:%d snmp:%d ipmi:%d jmx:%d", host->items_num,
				host->snmp_items_num, host->ipmi_items_num, host->jmx_items_num);

		/* 'tls_connect' and 'tls_accept' must be respected even if encryption support is not compiled in */
		treegix_log(LOG_LEVEL_TRACE, "  tls:[connect:%u accept:%u]", host->tls_connect, host->tls_accept);
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		treegix_log(LOG_LEVEL_TRACE, "  tls:[issuer:'%s' subject:'%s']", host->tls_issuer, host->tls_subject);

		if (NULL != host->tls_dc_psk)
		{
			treegix_log(LOG_LEVEL_TRACE, "  tls:[psk_identity:'%s' psk:'%s' dc_psk:%u]",
					host->tls_dc_psk->tls_psk_identity, host->tls_dc_psk->tls_psk,
					host->tls_dc_psk->refcount);
		}
#endif
		for (j = 0; j < host->interfaces_v.values_num; j++)
		{
			TRX_DC_INTERFACE	*interface = (TRX_DC_INTERFACE *)host->interfaces_v.values[j];

			treegix_log(LOG_LEVEL_TRACE, "  interfaceid:" TRX_FS_UI64, interface->interfaceid);
		}
	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_host_tags(void)
{
	trx_dc_host_tag_t	*host_tag;
	trx_dc_host_tag_index_t	*host_tag_index;
	trx_hashset_iter_t	iter;
	trx_vector_ptr_t	index;
	int			i;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->host_tags_index, &iter);

	while (NULL != (host_tag_index = (trx_dc_host_tag_index_t *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, host_tag_index);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		int	j;

		host_tag_index = (trx_dc_host_tag_index_t *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "hostid:" TRX_FS_UI64,  host_tag_index->hostid);

		for (j = 0; j < host_tag_index->tags.values_num; j++)
		{
			host_tag = (trx_dc_host_tag_t *)host_tag_index->tags.values[j];
			treegix_log(LOG_LEVEL_TRACE, "  '%s':'%s'", host_tag->tag, host_tag->value);
		}
	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_proxies(void)
{
	TRX_DC_PROXY		*proxy;
	trx_hashset_iter_t	iter;
	trx_vector_ptr_t	index;
	int			i;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->proxies, &iter);

	while (NULL != (proxy = (TRX_DC_PROXY *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, proxy);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		proxy = (TRX_DC_PROXY *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "hostid:" TRX_FS_UI64 " location:%u", proxy->hostid, proxy->location);
		treegix_log(LOG_LEVEL_TRACE, "  proxy_address:'%s'", proxy->proxy_address);
		treegix_log(LOG_LEVEL_TRACE, "  compress:%d", proxy->auto_compress);

	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_ipmihosts(void)
{
	TRX_DC_IPMIHOST		*ipmihost;
	trx_hashset_iter_t	iter;
	trx_vector_ptr_t	index;
	int			i;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->ipmihosts, &iter);

	while (NULL != (ipmihost = (TRX_DC_IPMIHOST *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, ipmihost);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		ipmihost = (TRX_DC_IPMIHOST *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "hostid:" TRX_FS_UI64 " ipmi:[username:'%s' password:'%s' authtype:%d"
				" privilege:%u]", ipmihost->hostid, ipmihost->ipmi_username, ipmihost->ipmi_password,
				ipmihost->ipmi_authtype, ipmihost->ipmi_privilege);
	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_host_inventories(void)
{
	TRX_DC_HOST_INVENTORY	*host_inventory;
	trx_hashset_iter_t	iter;
	trx_vector_ptr_t	index;
	int			i, j;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->host_inventories, &iter);

	while (NULL != (host_inventory = (TRX_DC_HOST_INVENTORY *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, host_inventory);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		host_inventory = (TRX_DC_HOST_INVENTORY *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "hostid:" TRX_FS_UI64 " inventory_mode:%u", host_inventory->hostid,
				host_inventory->inventory_mode);

		for (j = 0; j < HOST_INVENTORY_FIELD_COUNT; j++)
		{
			treegix_log(LOG_LEVEL_TRACE, "  %s: '%s'", DBget_inventory_field(j + 1),
					host_inventory->values[j]);
		}
	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "  End of %s()", __func__);
}

static void	DCdump_htmpls(void)
{
	TRX_DC_HTMPL		*htmpl = NULL;
	trx_hashset_iter_t	iter;
	trx_vector_ptr_t	index;
	int			i, j;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->htmpls, &iter);

	while (NULL != (htmpl = (TRX_DC_HTMPL *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, htmpl);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		htmpl = (TRX_DC_HTMPL *)index.values[i];

		treegix_log(LOG_LEVEL_TRACE, "hostid:" TRX_FS_UI64, htmpl->hostid);

		for (j = 0; j < htmpl->templateids.values_num; j++)
			treegix_log(LOG_LEVEL_TRACE, "  templateid:" TRX_FS_UI64, htmpl->templateids.values[j]);
	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_gmacros(void)
{
	TRX_DC_GMACRO		*gmacro;
	trx_hashset_iter_t	iter;
	trx_vector_ptr_t	index;
	int			i;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->gmacros, &iter);

	while (NULL != (gmacro = (TRX_DC_GMACRO *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, gmacro);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		gmacro = (TRX_DC_GMACRO *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "globalmacroid:" TRX_FS_UI64 " macro:'%s' value:'%s' context:'%s'",
				gmacro->globalmacroid, gmacro->macro,
				gmacro->value, TRX_NULL2EMPTY_STR(gmacro->context));
	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_hmacros(void)
{
	TRX_DC_HMACRO		*hmacro;
	trx_hashset_iter_t	iter;
	trx_vector_ptr_t	index;
	int			i;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->hmacros, &iter);

	while (NULL != (hmacro = (TRX_DC_HMACRO *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, hmacro);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		hmacro = (TRX_DC_HMACRO *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "hostmacroid:" TRX_FS_UI64 " hostid:" TRX_FS_UI64 " macro:'%s' value:'%s'"
				" context '%s'", hmacro->hostmacroid, hmacro->hostid, hmacro->macro, hmacro->value,
				TRX_NULL2EMPTY_STR(hmacro->context));
	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_interfaces(void)
{
	TRX_DC_INTERFACE	*interface;
	trx_hashset_iter_t	iter;
	trx_vector_ptr_t	index;
	int			i;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->interfaces, &iter);

	while (NULL != (interface = (TRX_DC_INTERFACE *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, interface);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		interface = (TRX_DC_INTERFACE *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "interfaceid:" TRX_FS_UI64 " hostid:" TRX_FS_UI64 " ip:'%s' dns:'%s'"
				" port:'%s' type:%u main:%u useip:%u bulk:%u",
				interface->interfaceid, interface->hostid, interface->ip, interface->dns,
				interface->port, interface->type, interface->main, interface->useip, interface->bulk);
	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_numitem(const TRX_DC_NUMITEM *numitem)
{
	treegix_log(LOG_LEVEL_TRACE, "  units:'%s' trends:%d", numitem->units, numitem->trends);
}

static void	DCdump_snmpitem(const TRX_DC_SNMPITEM *snmpitem)
{
	treegix_log(LOG_LEVEL_TRACE, "  snmp:[oid:'%s' community:'%s' oid_type:%u]", snmpitem->snmp_oid,
			snmpitem->snmp_community, snmpitem->snmp_oid_type);

	treegix_log(LOG_LEVEL_TRACE, "  snmpv3:[securityname:'%s' authpassphrase:'%s' privpassphrase:'%s']",
			snmpitem->snmpv3_securityname, snmpitem->snmpv3_authpassphrase,
			snmpitem->snmpv3_privpassphrase);

	treegix_log(LOG_LEVEL_TRACE, "  snmpv3:[contextname:'%s' securitylevel:%u authprotocol:%u privprotocol:%u]",
			snmpitem->snmpv3_contextname, snmpitem->snmpv3_securitylevel, snmpitem->snmpv3_authprotocol,
			snmpitem->snmpv3_privprotocol);
}

static void	DCdump_ipmiitem(const TRX_DC_IPMIITEM *ipmiitem)
{
	treegix_log(LOG_LEVEL_TRACE, "  ipmi_sensor:'%s'", ipmiitem->ipmi_sensor);
}

static void	DCdump_trapitem(const TRX_DC_TRAPITEM *trapitem)
{
	treegix_log(LOG_LEVEL_TRACE, "  trapper_hosts:'%s'", trapitem->trapper_hosts);
}

static void	DCdump_logitem(TRX_DC_LOGITEM *logitem)
{
	treegix_log(LOG_LEVEL_TRACE, "  logtimefmt:'%s'", logitem->logtimefmt);
}

static void	DCdump_dbitem(const TRX_DC_DBITEM *dbitem)
{
	treegix_log(LOG_LEVEL_TRACE, "  db:[params:'%s' username:'%s' password:'%s']", dbitem->params,
			dbitem->username, dbitem->password);
}

static void	DCdump_sshitem(const TRX_DC_SSHITEM *sshitem)
{
	treegix_log(LOG_LEVEL_TRACE, "  ssh:[username:'%s' password:'%s' authtype:%u params:'%s']",
			sshitem->username, sshitem->password, sshitem->authtype, sshitem->params);
	treegix_log(LOG_LEVEL_TRACE, "  ssh:[publickey:'%s' privatekey:'%s']", sshitem->publickey,
			sshitem->privatekey);
}

static void	DCdump_httpitem(const TRX_DC_HTTPITEM *httpitem)
{
	treegix_log(LOG_LEVEL_TRACE, "  http:[url:'%s']", httpitem->url);
	treegix_log(LOG_LEVEL_TRACE, "  http:[query fields:'%s']", httpitem->query_fields);
	treegix_log(LOG_LEVEL_TRACE, "  http:[headers:'%s']", httpitem->headers);
	treegix_log(LOG_LEVEL_TRACE, "  http:[posts:'%s']", httpitem->posts);

	treegix_log(LOG_LEVEL_TRACE, "  http:[timeout:'%s' status codes:'%s' follow redirects:%u post type:%u"
			" http proxy:'%s' retrieve mode:%u request method:%u output format:%u allow traps:%u"
			" trapper_hosts:'%s']",
			httpitem->timeout, httpitem->status_codes, httpitem->follow_redirects, httpitem->post_type,
			httpitem->http_proxy, httpitem->retrieve_mode, httpitem->request_method,
			httpitem->output_format, httpitem->allow_traps, httpitem->trapper_hosts);

	treegix_log(LOG_LEVEL_TRACE, "  http:[username:'%s' password:'%s' authtype:%u]",
			httpitem->username, httpitem->password, httpitem->authtype);
	treegix_log(LOG_LEVEL_TRACE, "  http:[publickey:'%s' privatekey:'%s' ssl key password:'%s' verify peer:%u"
			" verify host:%u]", httpitem->ssl_cert_file, httpitem->ssl_key_file, httpitem->ssl_key_password,
			httpitem->verify_peer, httpitem->verify_host);
}

static void	DCdump_telnetitem(const TRX_DC_TELNETITEM *telnetitem)
{
	treegix_log(LOG_LEVEL_TRACE, "  telnet:[username:'%s' password:'%s' params:'%s']", telnetitem->username,
			telnetitem->password, telnetitem->params);
}

static void	DCdump_simpleitem(const TRX_DC_SIMPLEITEM *simpleitem)
{
	treegix_log(LOG_LEVEL_TRACE, "  simple:[username:'%s' password:'%s']", simpleitem->username,
			simpleitem->password);
}

static void	DCdump_jmxitem(const TRX_DC_JMXITEM *jmxitem)
{
	treegix_log(LOG_LEVEL_TRACE, "  jmx:[username:'%s' password:'%s' endpoint:'%s']",
			jmxitem->username, jmxitem->password, jmxitem->jmx_endpoint);
}

static void	DCdump_calcitem(const TRX_DC_CALCITEM *calcitem)
{
	treegix_log(LOG_LEVEL_TRACE, "  calc:[params:'%s']", calcitem->params);
}

static void	DCdump_masteritem(const TRX_DC_MASTERITEM *masteritem)
{
	int	i;

	treegix_log(LOG_LEVEL_TRACE, "  dependent:");
	for (i = 0; i < masteritem->dep_itemids.values_num; i++)
	{
		treegix_log(LOG_LEVEL_TRACE, "    itemid:" TRX_FS_UI64 " flags:" TRX_FS_UI64,
				masteritem->dep_itemids.values[i].first, masteritem->dep_itemids.values[i].second);
	}
}

static void	DCdump_preprocitem(const TRX_DC_PREPROCITEM *preprocitem)
{
	int	i;

	treegix_log(LOG_LEVEL_TRACE, "  preprocessing:");
	treegix_log(LOG_LEVEL_TRACE, "  update_time:%d", preprocitem->update_time);

	for (i = 0; i < preprocitem->preproc_ops.values_num; i++)
	{
		trx_dc_preproc_op_t	*op = (trx_dc_preproc_op_t *)preprocitem->preproc_ops.values[i];
		treegix_log(LOG_LEVEL_TRACE, "      opid:" TRX_FS_UI64 " step:%d type:%u params:'%s'"
				" error_handler:%d error_handler_params:'%s'",
				op->item_preprocid, op->step, op->type, op->params, op->error_handler, op->error_handler_params);
	}
}

/* item type specific information debug logging support */

typedef void (*trx_dc_dump_func_t)(void *);

typedef struct
{
	trx_hashset_t		*hashset;
	trx_dc_dump_func_t	dump_func;
}
trx_trace_item_t;

static void	DCdump_items(void)
{
	TRX_DC_ITEM		*item;
	trx_hashset_iter_t	iter;
	int			i, j;
	trx_vector_ptr_t	index;
	void			*ptr;
	trx_trace_item_t	trace_items[] =
	{
		{&config->numitems, (trx_dc_dump_func_t)DCdump_numitem},
		{&config->snmpitems, (trx_dc_dump_func_t)DCdump_snmpitem},
		{&config->ipmiitems, (trx_dc_dump_func_t)DCdump_ipmiitem},
		{&config->trapitems, (trx_dc_dump_func_t)DCdump_trapitem},
		{&config->logitems, (trx_dc_dump_func_t)DCdump_logitem},
		{&config->dbitems, (trx_dc_dump_func_t)DCdump_dbitem},
		{&config->sshitems, (trx_dc_dump_func_t)DCdump_sshitem},
		{&config->telnetitems, (trx_dc_dump_func_t)DCdump_telnetitem},
		{&config->simpleitems, (trx_dc_dump_func_t)DCdump_simpleitem},
		{&config->jmxitems, (trx_dc_dump_func_t)DCdump_jmxitem},
		{&config->calcitems, (trx_dc_dump_func_t)DCdump_calcitem},
		{&config->masteritems, (trx_dc_dump_func_t)DCdump_masteritem},
		{&config->preprocitems, (trx_dc_dump_func_t)DCdump_preprocitem},
		{&config->httpitems, (trx_dc_dump_func_t)DCdump_httpitem},
	};

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->items, &iter);

	while (NULL != (item = (TRX_DC_ITEM *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, item);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		item = (TRX_DC_ITEM *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "itemid:" TRX_FS_UI64 " hostid:" TRX_FS_UI64 " key:'%s'",
				item->itemid, item->hostid, item->key);
		treegix_log(LOG_LEVEL_TRACE, "  type:%u value_type:%u", item->type, item->value_type);
		treegix_log(LOG_LEVEL_TRACE, "  interfaceid:" TRX_FS_UI64 " port:'%s'", item->interfaceid, item->port);
		treegix_log(LOG_LEVEL_TRACE, "  state:%u error:'%s'", item->state, item->error);
		treegix_log(LOG_LEVEL_TRACE, "  flags:%u status:%u", item->flags, item->status);
		treegix_log(LOG_LEVEL_TRACE, "  valuemapid:" TRX_FS_UI64, item->valuemapid);
		treegix_log(LOG_LEVEL_TRACE, "  lastlogsize:" TRX_FS_UI64 " mtime:%d", item->lastlogsize, item->mtime);
		treegix_log(LOG_LEVEL_TRACE, "  delay:'%s' nextcheck:%d lastclock:%d", item->delay, item->nextcheck,
				item->lastclock);
		treegix_log(LOG_LEVEL_TRACE, "  data_expected_from:%d", item->data_expected_from);
		treegix_log(LOG_LEVEL_TRACE, "  history:%d history_sec:%d", item->history, item->history_sec);
		treegix_log(LOG_LEVEL_TRACE, "  poller_type:%u location:%u", item->poller_type, item->location);
		treegix_log(LOG_LEVEL_TRACE, "  inventory_link:%u", item->inventory_link);
		treegix_log(LOG_LEVEL_TRACE, "  priority:%u schedulable:%u", item->queue_priority, item->schedulable);

		for (j = 0; j < (int)ARRSIZE(trace_items); j++)
		{
			if (NULL != (ptr = trx_hashset_search(trace_items[j].hashset, &item->itemid)))
				trace_items[j].dump_func(ptr);
		}

		if (NULL != item->triggers)
		{
			TRX_DC_TRIGGER	*trigger;

			treegix_log(LOG_LEVEL_TRACE, "  triggers:");

			for (j = 0; NULL != (trigger = item->triggers[j]); j++)
				treegix_log(LOG_LEVEL_TRACE, "    triggerid:" TRX_FS_UI64, trigger->triggerid);
		}
	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_interface_snmpitems(void)
{
	TRX_DC_INTERFACE_ITEM	*interface_snmpitem;
	trx_hashset_iter_t	iter;
	int			i, j;
	trx_vector_ptr_t	index;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->interface_snmpitems, &iter);

	while (NULL != (interface_snmpitem = (TRX_DC_INTERFACE_ITEM *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, interface_snmpitem);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		interface_snmpitem = (TRX_DC_INTERFACE_ITEM *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "interfaceid:" TRX_FS_UI64, interface_snmpitem->interfaceid);

		for (j = 0; j < interface_snmpitem->itemids.values_num; j++)
			treegix_log(LOG_LEVEL_TRACE, "  itemid:" TRX_FS_UI64, interface_snmpitem->itemids.values[j]);
	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_template_items(void)
{
	TRX_DC_TEMPLATE_ITEM	*template_item;
	trx_hashset_iter_t	iter;
	int			i;
	trx_vector_ptr_t	index;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->template_items, &iter);

	while (NULL != (template_item = (TRX_DC_TEMPLATE_ITEM *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, template_item);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		template_item = (TRX_DC_TEMPLATE_ITEM *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "itemid:" TRX_FS_UI64 " hostid:" TRX_FS_UI64 " templateid:" TRX_FS_UI64,
				template_item->itemid, template_item->hostid, template_item->templateid);
	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_master_items(void)
{
	TRX_DC_MASTERITEM	*master_item;
	trx_hashset_iter_t	iter;
	int			i, j;
	trx_vector_ptr_t	index;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->masteritems, &iter);

	while (NULL != (master_item = (TRX_DC_MASTERITEM *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, master_item);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		master_item = (TRX_DC_MASTERITEM *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "master itemid:" TRX_FS_UI64, master_item->itemid);

		for (j = 0; j < master_item->dep_itemids.values_num; j++)
		{
			treegix_log(LOG_LEVEL_TRACE, "  itemid:" TRX_FS_UI64 " flags:" TRX_FS_UI64,
					master_item->dep_itemids.values[j].first,
					master_item->dep_itemids.values[j].second);
		}
	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_prototype_items(void)
{
	TRX_DC_PROTOTYPE_ITEM	*proto_item;
	trx_hashset_iter_t	iter;
	int			i;
	trx_vector_ptr_t	index;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->template_items, &iter);

	while (NULL != (proto_item = (TRX_DC_PROTOTYPE_ITEM *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, proto_item);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		proto_item = (TRX_DC_PROTOTYPE_ITEM *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "itemid:" TRX_FS_UI64 " hostid:" TRX_FS_UI64 " templateid:" TRX_FS_UI64,
				proto_item->itemid, proto_item->hostid, proto_item->templateid);
	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_functions(void)
{
	TRX_DC_FUNCTION		*function;
	trx_hashset_iter_t	iter;
	int			i;
	trx_vector_ptr_t	index;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->functions, &iter);

	while (NULL != (function = (TRX_DC_FUNCTION *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, function);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		function = (TRX_DC_FUNCTION *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "functionid:" TRX_FS_UI64 " triggerid:" TRX_FS_UI64 " itemid:"
				TRX_FS_UI64 " function:'%s' parameter:'%s' timer:%u", function->functionid,
				function->triggerid, function->itemid, function->function, function->parameter,
				function->timer);

	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_trigger_tags(const TRX_DC_TRIGGER *trigger)
{
	int			i;
	trx_vector_ptr_t	index;

	trx_vector_ptr_create(&index);

	trx_vector_ptr_append_array(&index, trigger->tags.values, trigger->tags.values_num);
	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	treegix_log(LOG_LEVEL_TRACE, "  tags:");

	for (i = 0; i < index.values_num; i++)
	{
		trx_dc_trigger_tag_t	*tag = (trx_dc_trigger_tag_t *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "      tagid:" TRX_FS_UI64 " tag:'%s' value:'%s'",
				tag->triggertagid, tag->tag, tag->value);
	}

	trx_vector_ptr_destroy(&index);
}

static void	DCdump_triggers(void)
{
	TRX_DC_TRIGGER		*trigger;
	trx_hashset_iter_t	iter;
	int			i;
	trx_vector_ptr_t	index;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->triggers, &iter);

	while (NULL != (trigger = (TRX_DC_TRIGGER *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, trigger);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		trigger = (TRX_DC_TRIGGER *)index.values[i];

		treegix_log(LOG_LEVEL_TRACE, "triggerid:" TRX_FS_UI64 " description:'%s' type:%u status:%u priority:%u",
					trigger->triggerid, trigger->description, trigger->type, trigger->status,
					trigger->priority);
		treegix_log(LOG_LEVEL_TRACE, "  expression:'%s' recovery_expression:'%s'", trigger->expression,
				trigger->recovery_expression);
		treegix_log(LOG_LEVEL_TRACE, "  value:%u state:%u error:'%s' lastchange:%d", trigger->value,
				trigger->state, TRX_NULL2EMPTY_STR(trigger->error), trigger->lastchange);
		treegix_log(LOG_LEVEL_TRACE, "  correlation_tag:'%s' recovery_mode:'%u' correlation_mode:'%u'",
				trigger->correlation_tag, trigger->recovery_mode, trigger->correlation_mode);
		treegix_log(LOG_LEVEL_TRACE, "  topoindex:%u functional:%u locked:%u", trigger->topoindex,
				trigger->functional, trigger->locked);
		treegix_log(LOG_LEVEL_TRACE, "  opdata:'%s'", trigger->opdata);

		if (0 != trigger->tags.values_num)
			DCdump_trigger_tags(trigger);
	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_trigdeps(void)
{
	TRX_DC_TRIGGER_DEPLIST	*trigdep;
	trx_hashset_iter_t	iter;
	int			i, j;
	trx_vector_ptr_t	index;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->trigdeps, &iter);

	while (NULL != (trigdep = (TRX_DC_TRIGGER_DEPLIST *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, trigdep);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		trigdep = (TRX_DC_TRIGGER_DEPLIST *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "triggerid:" TRX_FS_UI64 " refcount:%d", trigdep->triggerid,
				trigdep->refcount);

		for (j = 0; j < trigdep->dependencies.values_num; j++)
		{
			const TRX_DC_TRIGGER_DEPLIST	*trigdep_up = (TRX_DC_TRIGGER_DEPLIST *)trigdep->dependencies.values[j];

			treegix_log(LOG_LEVEL_TRACE, "  triggerid:" TRX_FS_UI64, trigdep_up->triggerid);
		}
	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_expressions(void)
{
	TRX_DC_EXPRESSION	*expression;
	trx_hashset_iter_t	iter;
	int			i;
	trx_vector_ptr_t	index;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->expressions, &iter);

	while (NULL != (expression = (TRX_DC_EXPRESSION *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, expression);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		expression = (TRX_DC_EXPRESSION *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "expressionid:" TRX_FS_UI64 " regexp:'%s' expression:'%s delimiter:%d"
				" type:%u case_sensitive:%u", expression->expressionid, expression->regexp,
				expression->expression, expression->delimiter, expression->type,
				expression->case_sensitive);
	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_actions(void)
{
	trx_dc_action_t		*action;
	trx_hashset_iter_t	iter;
	int			i, j;
	trx_vector_ptr_t	index;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->actions, &iter);

	while (NULL != (action = (trx_dc_action_t *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, action);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		action = (trx_dc_action_t *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "actionid:" TRX_FS_UI64 " formula:'%s' eventsource:%u evaltype:%u"
				" opflags:%x", action->actionid, action->formula, action->eventsource, action->evaltype,
				action->opflags);

		for (j = 0; j < action->conditions.values_num; j++)
		{
			trx_dc_action_condition_t	*condition = (trx_dc_action_condition_t *)action->conditions.values[j];

			treegix_log(LOG_LEVEL_TRACE, "  conditionid:" TRX_FS_UI64 " conditiontype:%u operator:%u"
					" value:'%s' value2:'%s'", condition->conditionid, condition->conditiontype,
					condition->op, condition->value, condition->value2);
		}
	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_corr_conditions(trx_dc_correlation_t *correlation)
{
	int			i;
	trx_vector_ptr_t	index;

	trx_vector_ptr_create(&index);

	trx_vector_ptr_append_array(&index, correlation->conditions.values, correlation->conditions.values_num);
	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	treegix_log(LOG_LEVEL_TRACE, "  conditions:");

	for (i = 0; i < index.values_num; i++)
	{
		trx_dc_corr_condition_t	*condition = (trx_dc_corr_condition_t *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "      conditionid:" TRX_FS_UI64 " type:%d",
				condition->corr_conditionid, condition->type);

		switch (condition->type)
		{
			case TRX_CORR_CONDITION_EVENT_TAG_PAIR:
				treegix_log(LOG_LEVEL_TRACE, "        oldtag:'%s' newtag:'%s'",
						condition->data.tag_pair.oldtag, condition->data.tag_pair.newtag);
				break;
			case TRX_CORR_CONDITION_NEW_EVENT_HOSTGROUP:
				treegix_log(LOG_LEVEL_TRACE, "        groupid:" TRX_FS_UI64 " op:%u",
						condition->data.group.groupid, condition->data.group.op);
				break;
			case TRX_CORR_CONDITION_NEW_EVENT_TAG:
			case TRX_CORR_CONDITION_OLD_EVENT_TAG:
				treegix_log(LOG_LEVEL_TRACE, "        tag:'%s'", condition->data.tag.tag);
				break;
			case TRX_CORR_CONDITION_NEW_EVENT_TAG_VALUE:
			case TRX_CORR_CONDITION_OLD_EVENT_TAG_VALUE:
				treegix_log(LOG_LEVEL_TRACE, "        tag:'%s' value:'%s'",
						condition->data.tag_value.tag, condition->data.tag_value.value);
				break;
		}
	}

	trx_vector_ptr_destroy(&index);
}

static void	DCdump_corr_operations(trx_dc_correlation_t *correlation)
{
	int			i;
	trx_vector_ptr_t	index;

	trx_vector_ptr_create(&index);

	trx_vector_ptr_append_array(&index, correlation->operations.values, correlation->operations.values_num);
	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	treegix_log(LOG_LEVEL_TRACE, "  operations:");

	for (i = 0; i < index.values_num; i++)
	{
		trx_dc_corr_operation_t	*operation = (trx_dc_corr_operation_t *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "      operetionid:" TRX_FS_UI64 " type:%d",
				operation->corr_operationid, operation->type);
	}

	trx_vector_ptr_destroy(&index);
}

static void	DCdump_correlations(void)
{
	trx_dc_correlation_t	*correlation;
	trx_hashset_iter_t	iter;
	int			i;
	trx_vector_ptr_t	index;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->correlations, &iter);

	while (NULL != (correlation = (trx_dc_correlation_t *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, correlation);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		correlation = (trx_dc_correlation_t *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "correlationid:" TRX_FS_UI64 " name:'%s' evaltype:%u formula:'%s'",
				correlation->correlationid, correlation->name, correlation->evaltype,
				correlation->formula);

		DCdump_corr_conditions(correlation);
		DCdump_corr_operations(correlation);
	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_host_group_hosts(trx_dc_hostgroup_t *group)
{
	trx_hashset_iter_t	iter;
	int			i;
	trx_vector_uint64_t	index;
	trx_uint64_t		*phostid;

	trx_vector_uint64_create(&index);
	trx_hashset_iter_reset(&group->hostids, &iter);

	while (NULL != (phostid = (trx_uint64_t *)trx_hashset_iter_next(&iter)))
		trx_vector_uint64_append_ptr(&index, phostid);

	trx_vector_uint64_sort(&index, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	treegix_log(LOG_LEVEL_TRACE, "  hosts:");

	for (i = 0; i < index.values_num; i++)
		treegix_log(LOG_LEVEL_TRACE, "    hostid:" TRX_FS_UI64, index.values[i]);

	trx_vector_uint64_destroy(&index);
}

static void	DCdump_host_groups(void)
{
	trx_dc_hostgroup_t	*group;
	trx_hashset_iter_t	iter;
	int			i;
	trx_vector_ptr_t	index;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->hostgroups, &iter);

	while (NULL != (group = (trx_dc_hostgroup_t *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, group);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		group = (trx_dc_hostgroup_t *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "groupid:" TRX_FS_UI64 " name:'%s'", group->groupid, group->name);

		if (0 != group->hostids.num_data)
			DCdump_host_group_hosts(group);
	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_host_group_index(void)
{
	trx_dc_hostgroup_t	*group;
	int			i;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	treegix_log(LOG_LEVEL_TRACE, "group index:");

	for (i = 0; i < config->hostgroups_name.values_num; i++)
	{
		group = (trx_dc_hostgroup_t *)config->hostgroups_name.values[i];
		treegix_log(LOG_LEVEL_TRACE, "  %s", group->name);
	}

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

static void	DCdump_maintenance_groups(trx_dc_maintenance_t *maintenance)
{
	int			i;
	trx_vector_uint64_t	index;

	trx_vector_uint64_create(&index);

	if (0 != maintenance->groupids.values_num)
	{
		trx_vector_uint64_append_array(&index, maintenance->groupids.values, maintenance->groupids.values_num);
		trx_vector_uint64_sort(&index, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	}

	treegix_log(LOG_LEVEL_TRACE, "  groups:");

	for (i = 0; i < index.values_num; i++)
		treegix_log(LOG_LEVEL_TRACE, "    groupid:" TRX_FS_UI64, index.values[i]);

	trx_vector_uint64_destroy(&index);
}

static void	DCdump_maintenance_hosts(trx_dc_maintenance_t *maintenance)
{
	int			i;
	trx_vector_uint64_t	index;

	trx_vector_uint64_create(&index);

	if (0 != maintenance->hostids.values_num)
	{
		trx_vector_uint64_append_array(&index, maintenance->hostids.values, maintenance->hostids.values_num);
		trx_vector_uint64_sort(&index, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	}

	treegix_log(LOG_LEVEL_TRACE, "  hosts:");

	for (i = 0; i < index.values_num; i++)
		treegix_log(LOG_LEVEL_TRACE, "    hostid:" TRX_FS_UI64, index.values[i]);

	trx_vector_uint64_destroy(&index);
}

static int	maintenance_tag_compare(const void *v1, const void *v2)
{
	const trx_dc_maintenance_tag_t	*tag1 = *(const trx_dc_maintenance_tag_t **)v1;
	const trx_dc_maintenance_tag_t	*tag2 = *(const trx_dc_maintenance_tag_t **)v2;
	int				ret;

	if (0 != (ret = (strcmp(tag1->tag, tag2->tag))))
		return ret;

	if (0 != (ret = (strcmp(tag1->value, tag2->value))))
		return ret;

	TRX_RETURN_IF_NOT_EQUAL(tag1->op, tag2->op);

	return 0;
}

static void	DCdump_maintenance_tags(trx_dc_maintenance_t *maintenance)
{
	int			i;
	trx_vector_ptr_t	index;

	trx_vector_ptr_create(&index);

	if (0 != maintenance->tags.values_num)
	{
		trx_vector_ptr_append_array(&index, maintenance->tags.values, maintenance->tags.values_num);
		trx_vector_ptr_sort(&index, maintenance_tag_compare);
	}

	treegix_log(LOG_LEVEL_TRACE, "  tags:");

	for (i = 0; i < index.values_num; i++)
	{
		trx_dc_maintenance_tag_t	*tag = (trx_dc_maintenance_tag_t *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "    maintenancetagid:" TRX_FS_UI64 " operator:%u tag:'%s' value:'%s'",
				tag->maintenancetagid, tag->op, tag->tag, tag->value);
	}

	trx_vector_ptr_destroy(&index);
}

static void	DCdump_maintenance_periods(trx_dc_maintenance_t *maintenance)
{
	int			i;
	trx_vector_ptr_t	index;

	trx_vector_ptr_create(&index);

	trx_vector_ptr_append_array(&index, maintenance->periods.values, maintenance->periods.values_num);
	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	treegix_log(LOG_LEVEL_TRACE, "  periods:");

	for (i = 0; i < index.values_num; i++)
	{
		trx_dc_maintenance_period_t	*period = (trx_dc_maintenance_period_t *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "    timeperiodid:" TRX_FS_UI64 " type:%u every:%d month:%d dayofweek:%d"
				" day:%d start_time:%d period:%d start_date:%d",
				period->timeperiodid, period->type, period->every, period->month, period->dayofweek,
				period->day, period->start_time, period->period, period->start_date);
	}

	trx_vector_ptr_destroy(&index);
}

static void	DCdump_maintenances(void)
{
	trx_dc_maintenance_t	*maintenance;
	trx_hashset_iter_t	iter;
	int			i;
	trx_vector_ptr_t	index;

	treegix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	trx_vector_ptr_create(&index);
	trx_hashset_iter_reset(&config->maintenances, &iter);

	while (NULL != (maintenance = (trx_dc_maintenance_t *)trx_hashset_iter_next(&iter)))
		trx_vector_ptr_append(&index, maintenance);

	trx_vector_ptr_sort(&index, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < index.values_num; i++)
	{
		maintenance = (trx_dc_maintenance_t *)index.values[i];
		treegix_log(LOG_LEVEL_TRACE, "maintenanceid:" TRX_FS_UI64 " type:%u tag_evaltype:%u active_since:%d"
				" active_until:%d", maintenance->maintenanceid, maintenance->type,
				maintenance->tags_evaltype, maintenance->active_since, maintenance->active_until);
		treegix_log(LOG_LEVEL_TRACE, "  state:%u running_since:%d running_until:%d",
				maintenance->state, maintenance->running_since, maintenance->running_until);

		DCdump_maintenance_groups(maintenance);
		DCdump_maintenance_hosts(maintenance);
		DCdump_maintenance_tags(maintenance);
		DCdump_maintenance_periods(maintenance);
	}

	trx_vector_ptr_destroy(&index);

	treegix_log(LOG_LEVEL_TRACE, "End of %s()", __func__);
}

void	DCdump_configuration(void)
{
	DCdump_config();
	DCdump_hosts();
	DCdump_host_tags();
	DCdump_proxies();
	DCdump_ipmihosts();
	DCdump_host_inventories();
	DCdump_htmpls();
	DCdump_gmacros();
	DCdump_hmacros();
	DCdump_interfaces();
	DCdump_items();
	DCdump_interface_snmpitems();
	DCdump_template_items();
	DCdump_master_items();
	DCdump_prototype_items();
	DCdump_triggers();
	DCdump_trigdeps();
	DCdump_functions();
	DCdump_expressions();
	DCdump_actions();
	DCdump_correlations();
	DCdump_host_groups();
	DCdump_host_group_index();
	DCdump_maintenances();
}
