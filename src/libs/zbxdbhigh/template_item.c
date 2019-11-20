

#include "common.h"

#include "db.h"
#include "log.h"
#include "dbcache.h"
#include "trxserver.h"
#include "template.h"

typedef struct
{
	trx_uint64_t		itemid;
	trx_uint64_t		valuemapid;
	trx_uint64_t		interfaceid;
	trx_uint64_t		templateid;
	trx_uint64_t		master_itemid;
	char			*name;
	char			*key;
	char			*delay;
	char			*history;
	char			*trends;
	char			*trapper_hosts;
	char			*units;
	char			*formula;
	char			*logtimefmt;
	char			*params;
	char			*ipmi_sensor;
	char			*snmp_community;
	char			*snmp_oid;
	char			*snmpv3_securityname;
	char			*snmpv3_authpassphrase;
	char			*snmpv3_privpassphrase;
	char			*snmpv3_contextname;
	char			*username;
	char			*password;
	char			*publickey;
	char			*privatekey;
	char			*description;
	char			*lifetime;
	char			*port;
	char			*jmx_endpoint;
	char			*timeout;
	char			*url;
	char			*query_fields;
	char			*posts;
	char			*status_codes;
	char			*http_proxy;
	char			*headers;
	char			*ssl_cert_file;
	char			*ssl_key_file;
	char			*ssl_key_password;
	unsigned char		verify_peer;
	unsigned char		verify_host;
	unsigned char		follow_redirects;
	unsigned char		post_type;
	unsigned char		retrieve_mode;
	unsigned char		request_method;
	unsigned char		output_format;
	unsigned char		type;
	unsigned char		value_type;
	unsigned char		status;
	unsigned char		snmpv3_securitylevel;
	unsigned char		snmpv3_authprotocol;
	unsigned char		snmpv3_privprotocol;
	unsigned char		authtype;
	unsigned char		flags;
	unsigned char		inventory_link;
	unsigned char		evaltype;
	unsigned char		allow_traps;
	trx_vector_ptr_t	dependent_items;
}
trx_template_item_t;

/* lld rule condition */
typedef struct
{
	trx_uint64_t	item_conditionid;
	char		*macro;
	char		*value;
	unsigned char	op;
}
trx_lld_rule_condition_t;

/* lld rule */
typedef struct
{
	/* discovery rule source id */
	trx_uint64_t		templateid;
	/* discovery rule source conditions */
	trx_vector_ptr_t	conditions;

	/* discovery rule destination id */
	trx_uint64_t		itemid;
	/* the starting id to be used for destination condition ids */
	trx_uint64_t		conditionid;
	/* discovery rule destination condition ids */
	trx_vector_uint64_t	conditionids;
}
trx_lld_rule_map_t;

/* auxiliary function for DBcopy_template_items() */
static void	DBget_interfaces_by_hostid(trx_uint64_t hostid, trx_uint64_t *interfaceids)
{
	DB_RESULT	result;
	DB_ROW		row;
	unsigned char	type;

	result = DBselect(
			"select type,interfaceid"
			" from interface"
			" where hostid=" TRX_FS_UI64
				" and type in (%d,%d,%d,%d)"
				" and main=1",
			hostid, INTERFACE_TYPE_AGENT, INTERFACE_TYPE_SNMP, INTERFACE_TYPE_IPMI, INTERFACE_TYPE_JMX);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UCHAR(type, row[0]);
		TRX_STR2UINT64(interfaceids[type - 1], row[1]);
	}
	DBfree_result(result);
}

/******************************************************************************
 *                                                                            *
 * Function: get_template_items                                               *
 *                                                                            *
 * Purpose: read template items from database                                 *
 *                                                                            *
 * Parameters: hostid      - [IN] host id                                     *
 *             templateids - [IN] array of template IDs                       *
 *             items       - [OUT] the item data                              *
 *                                                                            *
 * Comments: The itemid and key are set depending on whether the item exists  *
 *           for the specified host.                                          *
 *           If item exists itemid will be set to its itemid and key will be  *
 *           set to NULL.                                                     *
 *           If item does not exist, itemid will be set to 0 and key will be  *
 *           set to item key.                                                 *
 *                                                                            *
 ******************************************************************************/
static void	get_template_items(trx_uint64_t hostid, const trx_vector_uint64_t *templateids, trx_vector_ptr_t *items)
{
	DB_RESULT		result;
	DB_ROW			row;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0, i;
	unsigned char		interface_type;
	trx_template_item_t	*item;
	trx_uint64_t		interfaceids[4];

	memset(&interfaceids, 0, sizeof(interfaceids));
	DBget_interfaces_by_hostid(hostid, interfaceids);

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select ti.itemid,ti.name,ti.key_,ti.type,ti.value_type,ti.delay,"
				"ti.history,ti.trends,ti.status,ti.trapper_hosts,ti.units,"
				"ti.formula,ti.logtimefmt,ti.valuemapid,ti.params,ti.ipmi_sensor,ti.snmp_community,"
				"ti.snmp_oid,ti.snmpv3_securityname,ti.snmpv3_securitylevel,ti.snmpv3_authprotocol,"
				"ti.snmpv3_authpassphrase,ti.snmpv3_privprotocol,ti.snmpv3_privpassphrase,ti.authtype,"
				"ti.username,ti.password,ti.publickey,ti.privatekey,ti.flags,ti.description,"
				"ti.inventory_link,ti.lifetime,ti.snmpv3_contextname,hi.itemid,ti.evaltype,ti.port,"
				"ti.jmx_endpoint,ti.master_itemid,ti.timeout,ti.url,ti.query_fields,ti.posts,"
				"ti.status_codes,ti.follow_redirects,ti.post_type,ti.http_proxy,ti.headers,"
				"ti.retrieve_mode,ti.request_method,ti.output_format,ti.ssl_cert_file,ti.ssl_key_file,"
				"ti.ssl_key_password,ti.verify_peer,ti.verify_host,ti.allow_traps"
			" from items ti"
			" left join items hi on hi.key_=ti.key_"
				" and hi.hostid=" TRX_FS_UI64
			" where",
			hostid);
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "ti.hostid", templateids->values, templateids->values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		item = (trx_template_item_t *)trx_malloc(NULL, sizeof(trx_template_item_t));

		TRX_STR2UINT64(item->templateid, row[0]);
		TRX_STR2UCHAR(item->type, row[3]);
		TRX_STR2UCHAR(item->value_type, row[4]);
		TRX_STR2UCHAR(item->status, row[8]);
		TRX_DBROW2UINT64(item->valuemapid, row[13]);
		TRX_STR2UCHAR(item->snmpv3_securitylevel, row[19]);
		TRX_STR2UCHAR(item->snmpv3_authprotocol, row[20]);
		TRX_STR2UCHAR(item->snmpv3_privprotocol, row[22]);
		TRX_STR2UCHAR(item->authtype, row[24]);
		TRX_STR2UCHAR(item->flags, row[29]);
		TRX_STR2UCHAR(item->inventory_link, row[31]);
		TRX_STR2UCHAR(item->evaltype, row[35]);

		switch (interface_type = get_interface_type_by_item_type(item->type))
		{
			case INTERFACE_TYPE_UNKNOWN:
				item->interfaceid = 0;
				break;
			case INTERFACE_TYPE_ANY:
				for (i = 0; INTERFACE_TYPE_COUNT > i; i++)
				{
					if (0 != interfaceids[INTERFACE_TYPE_PRIORITY[i] - 1])
						break;
				}
				item->interfaceid = interfaceids[INTERFACE_TYPE_PRIORITY[i] - 1];
				break;
			default:
				item->interfaceid = interfaceids[interface_type - 1];
		}

		item->name = trx_strdup(NULL, row[1]);
		item->delay = trx_strdup(NULL, row[5]);
		item->history = trx_strdup(NULL, row[6]);
		item->trends = trx_strdup(NULL, row[7]);
		item->trapper_hosts = trx_strdup(NULL, row[9]);
		item->units = trx_strdup(NULL, row[10]);
		item->formula = trx_strdup(NULL, row[11]);
		item->logtimefmt = trx_strdup(NULL, row[12]);
		item->params = trx_strdup(NULL, row[14]);
		item->ipmi_sensor = trx_strdup(NULL, row[15]);
		item->snmp_community = trx_strdup(NULL, row[16]);
		item->snmp_oid = trx_strdup(NULL, row[17]);
		item->snmpv3_securityname = trx_strdup(NULL, row[18]);
		item->snmpv3_authpassphrase = trx_strdup(NULL, row[21]);
		item->snmpv3_privpassphrase = trx_strdup(NULL, row[23]);
		item->username = trx_strdup(NULL, row[25]);
		item->password = trx_strdup(NULL, row[26]);
		item->publickey = trx_strdup(NULL, row[27]);
		item->privatekey = trx_strdup(NULL, row[28]);
		item->description = trx_strdup(NULL, row[30]);
		item->lifetime = trx_strdup(NULL, row[32]);
		item->snmpv3_contextname = trx_strdup(NULL, row[33]);
		item->port = trx_strdup(NULL, row[36]);
		item->jmx_endpoint = trx_strdup(NULL, row[37]);
		TRX_DBROW2UINT64(item->master_itemid, row[38]);

		if (SUCCEED != DBis_null(row[34]))
		{
			item->key = NULL;
			TRX_STR2UINT64(item->itemid, row[34]);
		}
		else
		{
			item->key = trx_strdup(NULL, row[2]);
			item->itemid = 0;
		}

		item->timeout = trx_strdup(NULL, row[39]);
		item->url = trx_strdup(NULL, row[40]);
		item->query_fields = trx_strdup(NULL, row[41]);
		item->posts = trx_strdup(NULL, row[42]);
		item->status_codes = trx_strdup(NULL, row[43]);
		TRX_STR2UCHAR(item->follow_redirects, row[44]);
		TRX_STR2UCHAR(item->post_type, row[45]);
		item->http_proxy = trx_strdup(NULL, row[46]);
		item->headers = trx_strdup(NULL, row[47]);
		TRX_STR2UCHAR(item->retrieve_mode, row[48]);
		TRX_STR2UCHAR(item->request_method, row[49]);
		TRX_STR2UCHAR(item->output_format, row[50]);
		item->ssl_cert_file = trx_strdup(NULL, row[51]);
		item->ssl_key_file = trx_strdup(NULL, row[52]);
		item->ssl_key_password = trx_strdup(NULL, row[53]);
		TRX_STR2UCHAR(item->verify_peer, row[54]);
		TRX_STR2UCHAR(item->verify_host, row[55]);
		TRX_STR2UCHAR(item->allow_traps, row[56]);
		trx_vector_ptr_create(&item->dependent_items);
		trx_vector_ptr_append(items, item);
	}
	DBfree_result(result);

	trx_free(sql);

	trx_vector_ptr_sort(items, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: get_template_lld_rule_map                                        *
 *                                                                            *
 * Purpose: reads template lld rule conditions and host lld_rule identifiers  *
 *          from database                                                     *
 *                                                                            *
 * Parameters: items - [IN] the host items including lld rules                *
 *             rules - [OUT] the ldd rule mapping                             *
 *                                                                            *
 ******************************************************************************/
static void	get_template_lld_rule_map(const trx_vector_ptr_t *items, trx_vector_ptr_t *rules)
{
	trx_template_item_t		*item;
	trx_lld_rule_map_t		*rule;
	trx_lld_rule_condition_t	*condition;
	int				i, index;
	trx_vector_uint64_t		itemids;
	DB_RESULT			result;
	DB_ROW				row;
	char				*sql = NULL;
	size_t				sql_alloc = 0, sql_offset = 0;
	trx_uint64_t			itemid, item_conditionid;

	trx_vector_uint64_create(&itemids);

	/* prepare discovery rules */
	for (i = 0; i < items->values_num; i++)
	{
		item = (trx_template_item_t *)items->values[i];

		if (0 == (TRX_FLAG_DISCOVERY_RULE & item->flags))
			continue;

		rule = (trx_lld_rule_map_t *)trx_malloc(NULL, sizeof(trx_lld_rule_map_t));

		rule->itemid = item->itemid;
		rule->templateid = item->templateid;
		rule->conditionid = 0;
		trx_vector_uint64_create(&rule->conditionids);
		trx_vector_ptr_create(&rule->conditions);

		trx_vector_ptr_append(rules, rule);

		if (0 != rule->itemid)
			trx_vector_uint64_append(&itemids, rule->itemid);
		trx_vector_uint64_append(&itemids, rule->templateid);
	}

	if (0 != itemids.values_num)
	{
		trx_vector_ptr_sort(rules, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
		trx_vector_uint64_sort(&itemids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select item_conditionid,itemid,operator,macro,value from item_condition where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid", itemids.values, itemids.values_num);

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			TRX_STR2UINT64(itemid, row[1]);

			index = trx_vector_ptr_bsearch(rules, &itemid, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

			if (FAIL != index)
			{
				/* read template lld conditions */

				rule = (trx_lld_rule_map_t *)rules->values[index];

				condition = (trx_lld_rule_condition_t *)trx_malloc(NULL, sizeof(trx_lld_rule_condition_t));

				TRX_STR2UINT64(condition->item_conditionid, row[0]);
				TRX_STR2UCHAR(condition->op, row[2]);
				condition->macro = trx_strdup(NULL, row[3]);
				condition->value = trx_strdup(NULL, row[4]);

				trx_vector_ptr_append(&rule->conditions, condition);
			}
			else
			{
				/* read host lld conditions identifiers */

				for (i = 0; i < rules->values_num; i++)
				{
					rule = (trx_lld_rule_map_t *)rules->values[i];

					if (itemid != rule->itemid)
						continue;

					TRX_STR2UINT64(item_conditionid, row[0]);
					trx_vector_uint64_append(&rule->conditionids, item_conditionid);

					break;
				}

				if (i == rules->values_num)
					THIS_SHOULD_NEVER_HAPPEN;
			}
		}
		DBfree_result(result);

		trx_free(sql);
	}

	trx_vector_uint64_destroy(&itemids);
}

/******************************************************************************
 *                                                                            *
 * Function: calculate_template_lld_rule_conditionids                         *
 *                                                                            *
 * Purpose: calculate identifiers for new item conditions                     *
 *                                                                            *
 * Parameters: rules - [IN] the ldd rule mapping                              *
 *                                                                            *
 * Return value: The number of new item conditions to be inserted.            *
 *                                                                            *
 ******************************************************************************/
static int	calculate_template_lld_rule_conditionids(trx_vector_ptr_t *rules)
{
	trx_lld_rule_map_t	*rule;
	int			i, conditions_num = 0;
	trx_uint64_t		conditionid;

	/* calculate the number of new conditions to be inserted */
	for (i = 0; i < rules->values_num; i++)
	{
		rule = (trx_lld_rule_map_t *)rules->values[i];

		if (rule->conditions.values_num > rule->conditionids.values_num)
			conditions_num += rule->conditions.values_num - rule->conditionids.values_num;
	}

	/* reserve ids for the new conditions to be inserted and assign to lld rules */
	if (0 == conditions_num)
		goto out;

	conditionid = DBget_maxid_num("item_condition", conditions_num);

	for (i = 0; i < rules->values_num; i++)
	{
		rule = (trx_lld_rule_map_t *)rules->values[i];

		if (rule->conditions.values_num <= rule->conditionids.values_num)
			continue;

		rule->conditionid = conditionid;
		conditionid += rule->conditions.values_num - rule->conditionids.values_num;
	}
out:
	return conditions_num;
}

/******************************************************************************
 *                                                                            *
 * Function: update_template_lld_rule_formulas                                *
 *                                                                            *
 * Purpose: translate template item condition identifiers in expression type  *
 *          discovery rule formulas to refer the host item condition          *
 *          identifiers instead.                                              *
 *                                                                            *
 * Parameters:  items  - [IN] the template items                              *
 *              rules  - [IN] the ldd rule mapping                            *
 *                                                                            *
 ******************************************************************************/
static void	update_template_lld_rule_formulas(trx_vector_ptr_t *items, trx_vector_ptr_t *rules)
{
	trx_lld_rule_map_t	*rule;
	int			i, j, index;
	char			*formula;
	trx_uint64_t		conditionid;

	for (i = 0; i < items->values_num; i++)
	{
		trx_template_item_t	*item = (trx_template_item_t *)items->values[i];

		if (0 == (TRX_FLAG_DISCOVERY_RULE & item->flags) || CONDITION_EVAL_TYPE_EXPRESSION != item->evaltype)
			continue;

		index = trx_vector_ptr_bsearch(rules, &item->templateid, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

		if (FAIL == index)
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		rule = (trx_lld_rule_map_t *)rules->values[index];

		formula = trx_strdup(NULL, item->formula);

		conditionid = rule->conditionid;

		for (j = 0; j < rule->conditions.values_num; j++)
		{
			trx_uint64_t			id;
			char				srcid[64], dstid[64], *ptr;
			size_t				pos = 0, len;

			trx_lld_rule_condition_t	*condition = (trx_lld_rule_condition_t *)rule->conditions.values[j];

			if (j < rule->conditionids.values_num)
				id = rule->conditionids.values[j];
			else
				id = conditionid++;

			trx_snprintf(srcid, sizeof(srcid), "{" TRX_FS_UI64 "}", condition->item_conditionid);
			trx_snprintf(dstid, sizeof(dstid), "{" TRX_FS_UI64 "}", id);

			len = strlen(srcid);

			while (NULL != (ptr = strstr(formula + pos, srcid)))
			{
				pos = ptr - formula + len - 1;
				trx_replace_string(&formula, ptr - formula, &pos, dstid);
			}
		}

		trx_free(item->formula);
		item->formula = formula;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: save_template_item                                               *
 *                                                                            *
 * Purpose: save (insert or update) template item                             *
 *                                                                            *
 * Parameters: hostid            - [IN] parent host id                        *
 *             itemid            - [IN/OUT] item id used for insert           *
 *                                          operations                        *
 *             item              - [IN] item to be saved                      *
 *             db_insert_items   - [IN] prepared item bulk insert             *
 *             db_insert_irtdata - [IN] prepared item discovery bulk insert   *
 *             sql               - [IN/OUT] sql buffer pointer used for       *
 *                                          update operations                 *
 *             sql_alloc         - [IN/OUT] sql buffer already allocated      *
 *                                          memory                            *
 *             sql_offset        - [IN/OUT] offset for writing within sql     *
 *                                          buffer                            *
 *                                                                            *
 ******************************************************************************/
static void	save_template_item(trx_uint64_t hostid, trx_uint64_t *itemid, trx_template_item_t *item,
		trx_db_insert_t *db_insert_items, trx_db_insert_t *db_insert_irtdata, char **sql, size_t *sql_alloc,
		size_t *sql_offset)
{
	int			i;
	trx_template_item_t	*dependent;

	if (NULL == item->key) /* existing item */
	{
		char	*name_esc, *delay_esc, *history_esc, *trends_esc, *trapper_hosts_esc, *units_esc, *formula_esc,
			*logtimefmt_esc, *params_esc, *ipmi_sensor_esc, *snmp_community_esc, *snmp_oid_esc,
			*snmpv3_securityname_esc, *snmpv3_authpassphrase_esc, *snmpv3_privpassphrase_esc, *username_esc,
			*password_esc, *publickey_esc, *privatekey_esc, *description_esc, *lifetime_esc,
			*snmpv3_contextname_esc, *port_esc, *jmx_endpoint_esc, *timeout_esc, *url_esc,
			*query_fields_esc, *posts_esc, *status_codes_esc, *http_proxy_esc, *headers_esc,
			*ssl_cert_file_esc, *ssl_key_file_esc, *ssl_key_password_esc;

		name_esc = DBdyn_escape_string(item->name);
		delay_esc = DBdyn_escape_string(item->delay);
		history_esc = DBdyn_escape_string(item->history);
		trends_esc = DBdyn_escape_string(item->trends);
		trapper_hosts_esc = DBdyn_escape_string(item->trapper_hosts);
		units_esc = DBdyn_escape_string(item->units);
		formula_esc = DBdyn_escape_string(item->formula);
		logtimefmt_esc = DBdyn_escape_string(item->logtimefmt);
		params_esc = DBdyn_escape_string(item->params);
		ipmi_sensor_esc = DBdyn_escape_string(item->ipmi_sensor);
		snmp_community_esc = DBdyn_escape_string(item->snmp_community);
		snmp_oid_esc = DBdyn_escape_string(item->snmp_oid);
		snmpv3_securityname_esc = DBdyn_escape_string(item->snmpv3_securityname);
		snmpv3_authpassphrase_esc = DBdyn_escape_string(item->snmpv3_authpassphrase);
		snmpv3_privpassphrase_esc = DBdyn_escape_string(item->snmpv3_privpassphrase);
		username_esc = DBdyn_escape_string(item->username);
		password_esc = DBdyn_escape_string(item->password);
		publickey_esc = DBdyn_escape_string(item->publickey);
		privatekey_esc = DBdyn_escape_string(item->privatekey);
		description_esc = DBdyn_escape_string(item->description);
		lifetime_esc = DBdyn_escape_string(item->lifetime);
		snmpv3_contextname_esc = DBdyn_escape_string(item->snmpv3_contextname);
		port_esc = DBdyn_escape_string(item->port);
		jmx_endpoint_esc = DBdyn_escape_string(item->jmx_endpoint);
		timeout_esc = DBdyn_escape_string(item->timeout);
		url_esc = DBdyn_escape_string(item->url);
		query_fields_esc = DBdyn_escape_string(item->query_fields);
		posts_esc = DBdyn_escape_string(item->posts);
		status_codes_esc = DBdyn_escape_string(item->status_codes);
		http_proxy_esc = DBdyn_escape_string(item->http_proxy);
		headers_esc = DBdyn_escape_string(item->headers);
		ssl_cert_file_esc = DBdyn_escape_string(item->ssl_cert_file);
		ssl_key_file_esc = DBdyn_escape_string(item->ssl_key_file);
		ssl_key_password_esc = DBdyn_escape_string(item->ssl_key_password);

		trx_snprintf_alloc(sql, sql_alloc, sql_offset,
				"update items"
				" set name='%s',"
					"type=%d,"
					"value_type=%d,"
					"delay='%s',"
					"history='%s',"
					"trends='%s',"
					"status=%d,"
					"trapper_hosts='%s',"
					"units='%s',"
					"formula='%s',"
					"logtimefmt='%s',"
					"valuemapid=%s,"
					"params='%s',"
					"ipmi_sensor='%s',"
					"snmp_community='%s',"
					"snmp_oid='%s',"
					"snmpv3_securityname='%s',"
					"snmpv3_securitylevel=%d,"
					"snmpv3_authprotocol=%d,"
					"snmpv3_authpassphrase='%s',"
					"snmpv3_privprotocol=%d,"
					"snmpv3_privpassphrase='%s',"
					"snmpv3_contextname='%s',"
					"authtype=%d,"
					"username='%s',"
					"password='%s',"
					"publickey='%s',"
					"privatekey='%s',"
					"templateid=" TRX_FS_UI64 ","
					"flags=%d,"
					"description='%s',"
					"inventory_link=%d,"
					"interfaceid=%s,"
					"lifetime='%s',"
					"evaltype=%d,"
					"port='%s',"
					"jmx_endpoint='%s',"
					"master_itemid=%s,"
					"timeout='%s',"
					"url='%s',"
					"query_fields='%s',"
					"posts='%s',"
					"status_codes='%s',"
					"follow_redirects=%d,"
					"post_type=%d,"
					"http_proxy='%s',"
					"headers='%s',"
					"retrieve_mode=%d,"
					"request_method=%d,"
					"output_format=%d,"
					"ssl_cert_file='%s',"
					"ssl_key_file='%s',"
					"ssl_key_password='%s',"
					"verify_peer=%d,"
					"verify_host=%d,"
					"allow_traps=%d"
				" where itemid=" TRX_FS_UI64 ";\n",
				name_esc, (int)item->type, (int)item->value_type, delay_esc,
				history_esc, trends_esc, (int)item->status, trapper_hosts_esc, units_esc,
				formula_esc, logtimefmt_esc, DBsql_id_ins(item->valuemapid), params_esc,
				ipmi_sensor_esc, snmp_community_esc, snmp_oid_esc, snmpv3_securityname_esc,
				(int)item->snmpv3_securitylevel, (int)item->snmpv3_authprotocol,
				snmpv3_authpassphrase_esc, (int)item->snmpv3_privprotocol, snmpv3_privpassphrase_esc,
				snmpv3_contextname_esc, (int)item->authtype, username_esc, password_esc, publickey_esc,
				privatekey_esc, item->templateid, (int)item->flags, description_esc,
				(int)item->inventory_link, DBsql_id_ins(item->interfaceid), lifetime_esc,
				(int)item->evaltype, port_esc, jmx_endpoint_esc, DBsql_id_ins(item->master_itemid),
				timeout_esc, url_esc, query_fields_esc, posts_esc, status_codes_esc,
				item->follow_redirects, item->post_type, http_proxy_esc, headers_esc,
				item->retrieve_mode, item->request_method, item->output_format, ssl_cert_file_esc,
				ssl_key_file_esc, ssl_key_password_esc, item->verify_peer, item->verify_host,
				item->allow_traps, item->itemid);

		trx_free(jmx_endpoint_esc);
		trx_free(port_esc);
		trx_free(snmpv3_contextname_esc);
		trx_free(lifetime_esc);
		trx_free(description_esc);
		trx_free(privatekey_esc);
		trx_free(publickey_esc);
		trx_free(password_esc);
		trx_free(username_esc);
		trx_free(snmpv3_privpassphrase_esc);
		trx_free(snmpv3_authpassphrase_esc);
		trx_free(snmpv3_securityname_esc);
		trx_free(snmp_oid_esc);
		trx_free(snmp_community_esc);
		trx_free(ipmi_sensor_esc);
		trx_free(params_esc);
		trx_free(logtimefmt_esc);
		trx_free(formula_esc);
		trx_free(units_esc);
		trx_free(trapper_hosts_esc);
		trx_free(trends_esc);
		trx_free(history_esc);
		trx_free(delay_esc);
		trx_free(name_esc);
		trx_free(timeout_esc);
		trx_free(url_esc);
		trx_free(query_fields_esc);
		trx_free(posts_esc);
		trx_free(status_codes_esc);
		trx_free(http_proxy_esc);
		trx_free(headers_esc);
		trx_free(ssl_cert_file_esc);
		trx_free(ssl_key_file_esc);
		trx_free(ssl_key_password_esc);
	}
	else
	{
		trx_db_insert_add_values(db_insert_items, *itemid, item->name, item->key, hostid, (int)item->type,
				(int)item->value_type, item->delay, item->history, item->trends,
				(int)item->status, item->trapper_hosts, item->units, item->formula, item->logtimefmt,
				item->valuemapid, item->params, item->ipmi_sensor, item->snmp_community, item->snmp_oid,
				item->snmpv3_securityname, (int)item->snmpv3_securitylevel,
				(int)item->snmpv3_authprotocol, item->snmpv3_authpassphrase,
				(int)item->snmpv3_privprotocol, item->snmpv3_privpassphrase, (int)item->authtype,
				item->username, item->password, item->publickey, item->privatekey, item->templateid,
				(int)item->flags, item->description, (int)item->inventory_link, item->interfaceid,
				item->lifetime, item->snmpv3_contextname, (int)item->evaltype, item->port,
				item->jmx_endpoint, item->master_itemid, item->timeout, item->url, item->query_fields,
				item->posts, item->status_codes, item->follow_redirects, item->post_type,
				item->http_proxy, item->headers, item->retrieve_mode, item->request_method,
				item->output_format, item->ssl_cert_file, item->ssl_key_file, item->ssl_key_password,
				item->verify_peer, item->verify_host, item->allow_traps);

		trx_db_insert_add_values(db_insert_irtdata, *itemid);

		item->itemid = (*itemid)++;
	}

	for (i = 0; i < item->dependent_items.values_num; i++)
	{
		dependent = (trx_template_item_t *)item->dependent_items.values[i];
		dependent->master_itemid = item->itemid;
		save_template_item(hostid, itemid, dependent, db_insert_items, db_insert_irtdata, sql, sql_alloc,
				sql_offset);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: save_template_items                                              *
 *                                                                            *
 * Purpose: saves template items to the target host in database               *
 *                                                                            *
 * Parameters:  hostid - [IN] the target host                                 *
 *              items  - [IN] the template items                              *
 *                                                                            *
 ******************************************************************************/
static void	save_template_items(trx_uint64_t hostid, trx_vector_ptr_t *items)
{
	char			*sql = NULL;
	size_t			sql_alloc = 16 * TRX_KIBIBYTE, sql_offset = 0;
	int			new_items = 0, upd_items = 0, i;
	trx_uint64_t		itemid = 0;
	trx_db_insert_t		db_insert_items, db_insert_irtdata;
	trx_template_item_t	*item;

	if (0 == items->values_num)
		return;

	for (i = 0; i < items->values_num; i++)
	{
		item = (trx_template_item_t *)items->values[i];

		if (NULL == item->key)
			upd_items++;
		else
			new_items++;
	}

	if (0 != new_items)
	{
		itemid = DBget_maxid_num("items", new_items);

		trx_db_insert_prepare(&db_insert_items, "items", "itemid", "name", "key_", "hostid", "type", "value_type",
				"delay", "history", "trends", "status", "trapper_hosts", "units",
				"formula", "logtimefmt", "valuemapid", "params", "ipmi_sensor",
				"snmp_community", "snmp_oid", "snmpv3_securityname", "snmpv3_securitylevel",
				"snmpv3_authprotocol", "snmpv3_authpassphrase", "snmpv3_privprotocol",
				"snmpv3_privpassphrase", "authtype", "username", "password", "publickey", "privatekey",
				"templateid", "flags", "description", "inventory_link", "interfaceid", "lifetime",
				"snmpv3_contextname", "evaltype", "port", "jmx_endpoint", "master_itemid",
				"timeout", "url", "query_fields", "posts", "status_codes", "follow_redirects",
				"post_type", "http_proxy", "headers", "retrieve_mode", "request_method",
				"output_format", "ssl_cert_file", "ssl_key_file", "ssl_key_password", "verify_peer",
				"verify_host", "allow_traps", NULL);

		trx_db_insert_prepare(&db_insert_irtdata, "item_rtdata", "itemid", NULL);
	}

	if (0 != upd_items)
	{
		sql = (char *)trx_malloc(sql, sql_alloc);
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);
	}

	for (i = 0; i < items->values_num; i++)
	{
		item = (trx_template_item_t *)items->values[i];

		/* dependent items are saved within recursive save_template_item calls while saving master */
		if (0 == item->master_itemid)
		{
			save_template_item(hostid, &itemid, item, &db_insert_items, &db_insert_irtdata,
					&sql, &sql_alloc, &sql_offset);
		}
	}

	if (0 != new_items)
	{
		trx_db_insert_execute(&db_insert_items);
		trx_db_insert_clean(&db_insert_items);

		trx_db_insert_execute(&db_insert_irtdata);
		trx_db_insert_clean(&db_insert_irtdata);
	}

	if (0 != upd_items)
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (16 < sql_offset)
			DBexecute("%s", sql);

		trx_free(sql);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: save_template_lld_rules                                          *
 *                                                                            *
 * Purpose: saves template lld rule item conditions to the target host in     *
 *          database                                                          *
 *                                                                            *
 * Parameters:  items          - [IN] the template items                      *
 *              rules          - [IN] the ldd rule mapping                    *
 *              new_conditions - [IN] the number of new item conditions to    *
 *                                    be inserted                             *
 *                                                                            *
 ******************************************************************************/
static void	save_template_lld_rules(trx_vector_ptr_t *items, trx_vector_ptr_t *rules, int new_conditions)
{
	char				*macro_esc, *value_esc;
	int				i, j, index;
	trx_db_insert_t			db_insert;
	trx_lld_rule_map_t		*rule;
	trx_lld_rule_condition_t	*condition;
	char				*sql = NULL;
	size_t				sql_alloc = 0, sql_offset = 0;
	trx_vector_uint64_t		item_conditionids;

	if (0 == rules->values_num)
		return;

	trx_vector_uint64_create(&item_conditionids);

	if (0 != new_conditions)
	{
		trx_db_insert_prepare(&db_insert, "item_condition", "item_conditionid", "itemid", "operator", "macro",
				"value", NULL);

		/* insert lld rule conditions for new items */
		for (i = 0; i < items->values_num; i++)
		{
			trx_template_item_t	*item = (trx_template_item_t *)items->values[i];

			if (NULL == item->key)
				continue;

			if (0 == (TRX_FLAG_DISCOVERY_RULE & item->flags))
				continue;

			index = trx_vector_ptr_bsearch(rules, &item->templateid, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

			if (FAIL == index)
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			rule = (trx_lld_rule_map_t *)rules->values[index];

			for (j = 0; j < rule->conditions.values_num; j++)
			{
				condition = (trx_lld_rule_condition_t *)rule->conditions.values[j];

				trx_db_insert_add_values(&db_insert, rule->conditionid++, item->itemid,
						(int)condition->op, condition->macro, condition->value);
			}
		}
	}

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* update lld rule conditions for existing items */
	for (i = 0; i < rules->values_num; i++)
	{
		rule = (trx_lld_rule_map_t *)rules->values[i];

		/* skip lld rules of new items */
		if (0 == rule->itemid)
			continue;

		index = MIN(rule->conditions.values_num, rule->conditionids.values_num);

		/* update intersecting rule conditions */
		for (j = 0; j < index; j++)
		{
			condition = (trx_lld_rule_condition_t *)rule->conditions.values[j];

			macro_esc = DBdyn_escape_string(condition->macro);
			value_esc = DBdyn_escape_string(condition->value);

			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update item_condition"
					" set operator=%d,macro='%s',value='%s'"
					" where item_conditionid=" TRX_FS_UI64 ";\n",
					(int)condition->op, macro_esc, value_esc, rule->conditionids.values[j]);

			DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);

			trx_free(value_esc);
			trx_free(macro_esc);
		}

		/* delete removed rule conditions */
		for (j = index; j < rule->conditionids.values_num; j++)
			trx_vector_uint64_append(&item_conditionids, rule->conditionids.values[j]);

		/* insert new rule conditions */
		for (j = index; j < rule->conditions.values_num; j++)
		{
			condition = (trx_lld_rule_condition_t *)rule->conditions.values[j];

			trx_db_insert_add_values(&db_insert, rule->conditionid++, rule->itemid,
					(int)condition->op, condition->macro, condition->value);
		}
	}

	/* delete removed item conditions */
	if (0 != item_conditionids.values_num)
	{
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from item_condition where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "item_conditionid", item_conditionids.values,
				item_conditionids.values_num);
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (16 < sql_offset)
		DBexecute("%s", sql);

	if (0 != new_conditions)
	{
		trx_db_insert_execute(&db_insert);
		trx_db_insert_clean(&db_insert);
	}

	trx_free(sql);
	trx_vector_uint64_destroy(&item_conditionids);
}

/******************************************************************************
 *                                                                            *
 * Function: save_template_item_applications                                  *
 *                                                                            *
 * Purpose: saves new item applications links in database                     *
 *                                                                            *
 * Parameters:  items   - [IN] the template items                             *
 *                                                                            *
 ******************************************************************************/
static void	save_template_item_applications(trx_vector_ptr_t *items)
{
	typedef struct
	{
		trx_uint64_t	itemid;
		trx_uint64_t	applicationid;
	}
	trx_itemapp_t;

	DB_RESULT		result;
	DB_ROW			row;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	trx_vector_uint64_t	itemids;
	trx_vector_ptr_t	itemapps;
	trx_itemapp_t		*itemapp;
	int			i;
	trx_db_insert_t		db_insert;

	trx_vector_ptr_create(&itemapps);
	trx_vector_uint64_create(&itemids);

	for (i = 0; i < items->values_num; i++)
	{
		trx_template_item_t	*item = (trx_template_item_t *)items->values[i];

		trx_vector_uint64_append(&itemids, item->itemid);
	}

	trx_vector_uint64_sort(&itemids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select hi.itemid,ha.applicationid"
			" from items_applications tia"
				" join items hi on hi.templateid=tia.itemid"
					" and");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hi.itemid", itemids.values, itemids.values_num);
	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				" join application_template hat on hat.templateid=tia.applicationid"
				" join applications ha on ha.applicationid=hat.applicationid"
					" and ha.hostid=hi.hostid"
					" left join items_applications hia on hia.applicationid=ha.applicationid"
						" and hia.itemid=hi.itemid"
			" where hia.itemappid is null");

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		itemapp = (trx_itemapp_t *)trx_malloc(NULL, sizeof(trx_itemapp_t));

		TRX_STR2UINT64(itemapp->itemid, row[0]);
		TRX_STR2UINT64(itemapp->applicationid, row[1]);

		trx_vector_ptr_append(&itemapps, itemapp);
	}
	DBfree_result(result);

	if (0 == itemapps.values_num)
		goto out;

	trx_db_insert_prepare(&db_insert, "items_applications", "itemappid", "itemid", "applicationid", NULL);

	for (i = 0; i < itemapps.values_num; i++)
	{
		itemapp = (trx_itemapp_t *)itemapps.values[i];

		trx_db_insert_add_values(&db_insert, __UINT64_C(0), itemapp->itemid, itemapp->applicationid);
	}

	trx_db_insert_autoincrement(&db_insert, "itemappid");
	trx_db_insert_execute(&db_insert);
	trx_db_insert_clean(&db_insert);
out:
	trx_free(sql);

	trx_vector_uint64_destroy(&itemids);

	trx_vector_ptr_clear_ext(&itemapps, trx_ptr_free);
	trx_vector_ptr_destroy(&itemapps);
}

/******************************************************************************
 *                                                                            *
 * Function: save_template_discovery_prototypes                               *
 *                                                                            *
 * Purpose: saves host item prototypes in database                            *
 *                                                                            *
 * Parameters:  hostid  - [IN] the target host                                *
 *              items   - [IN] the template items                             *
 *                                                                            *
 ******************************************************************************/
static void	save_template_discovery_prototypes(trx_uint64_t hostid, trx_vector_ptr_t *items)
{
	typedef struct
	{
		trx_uint64_t	itemid;
		trx_uint64_t	parent_itemid;
	}
	trx_proto_t;

	DB_RESULT		result;
	DB_ROW			row;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	trx_vector_uint64_t	itemids;
	trx_vector_ptr_t	prototypes;
	trx_proto_t		*proto;
	int			i;
	trx_db_insert_t		db_insert;

	trx_vector_ptr_create(&prototypes);
	trx_vector_uint64_create(&itemids);

	for (i = 0; i < items->values_num; i++)
	{
		trx_template_item_t	*item = (trx_template_item_t *)items->values[i];

		/* process only new prototype items */
		if (NULL == item->key || 0 == (TRX_FLAG_DISCOVERY_PROTOTYPE & item->flags))
			continue;

		trx_vector_uint64_append(&itemids, item->itemid);
	}

	if (0 == itemids.values_num)
		goto out;

	trx_vector_uint64_sort(&itemids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select i.itemid,r.itemid"
			" from items i,item_discovery id,items r"
			" where i.templateid=id.itemid"
				" and id.parent_itemid=r.templateid"
				" and r.hostid=" TRX_FS_UI64
				" and",
			hostid);
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "i.itemid", itemids.values, itemids.values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		proto = (trx_proto_t *)trx_malloc(NULL, sizeof(trx_proto_t));

		TRX_STR2UINT64(proto->itemid, row[0]);
		TRX_STR2UINT64(proto->parent_itemid, row[1]);

		trx_vector_ptr_append(&prototypes, proto);
	}
	DBfree_result(result);

	if (0 == prototypes.values_num)
		goto out;

	trx_db_insert_prepare(&db_insert, "item_discovery", "itemdiscoveryid", "itemid",
					"parent_itemid", NULL);

	for (i = 0; i < prototypes.values_num; i++)
	{
		proto = (trx_proto_t *)prototypes.values[i];

		trx_db_insert_add_values(&db_insert, __UINT64_C(0), proto->itemid, proto->parent_itemid);
	}

	trx_db_insert_autoincrement(&db_insert, "itemdiscoveryid");
	trx_db_insert_execute(&db_insert);
	trx_db_insert_clean(&db_insert);
out:
	trx_free(sql);

	trx_vector_uint64_destroy(&itemids);

	trx_vector_ptr_clear_ext(&prototypes, trx_ptr_free);
	trx_vector_ptr_destroy(&prototypes);
}

/******************************************************************************
 *                                                                            *
 * Function: free_template_item                                               *
 *                                                                            *
 * Purpose: frees template item                                               *
 *                                                                            *
 * Parameters:  item  - [IN] the template item                                *
 *                                                                            *
 ******************************************************************************/
static void	free_template_item(trx_template_item_t *item)
{
	trx_free(item->timeout);
	trx_free(item->url);
	trx_free(item->query_fields);
	trx_free(item->posts);
	trx_free(item->status_codes);
	trx_free(item->http_proxy);
	trx_free(item->headers);
	trx_free(item->ssl_cert_file);
	trx_free(item->ssl_key_file);
	trx_free(item->ssl_key_password);
	trx_free(item->jmx_endpoint);
	trx_free(item->port);
	trx_free(item->snmpv3_contextname);
	trx_free(item->lifetime);
	trx_free(item->description);
	trx_free(item->privatekey);
	trx_free(item->publickey);
	trx_free(item->password);
	trx_free(item->username);
	trx_free(item->snmpv3_privpassphrase);
	trx_free(item->snmpv3_authpassphrase);
	trx_free(item->snmpv3_securityname);
	trx_free(item->snmp_oid);
	trx_free(item->snmp_community);
	trx_free(item->ipmi_sensor);
	trx_free(item->params);
	trx_free(item->logtimefmt);
	trx_free(item->formula);
	trx_free(item->units);
	trx_free(item->trapper_hosts);
	trx_free(item->trends);
	trx_free(item->history);
	trx_free(item->delay);
	trx_free(item->name);
	trx_free(item->key);

	trx_vector_ptr_destroy(&item->dependent_items);

	trx_free(item);
}

/******************************************************************************
 *                                                                            *
 * Function: free_lld_rule_condition                                          *
 *                                                                            *
 * Purpose: frees lld rule condition                                          *
 *                                                                            *
 * Parameters:  item  - [IN] the lld rule condition                           *
 *                                                                            *
 ******************************************************************************/
static void	free_lld_rule_condition(trx_lld_rule_condition_t *condition)
{
	trx_free(condition->macro);
	trx_free(condition->value);
	trx_free(condition);
}

/******************************************************************************
 *                                                                            *
 * Function: free_lld_rule_map                                                *
 *                                                                            *
 * Purpose: frees lld rule mapping                                            *
 *                                                                            *
 * Parameters:  item  - [IN] the lld rule mapping                             *
 *                                                                            *
 ******************************************************************************/
static void	free_lld_rule_map(trx_lld_rule_map_t *rule)
{
	trx_vector_ptr_clear_ext(&rule->conditions, (trx_clean_func_t)free_lld_rule_condition);
	trx_vector_ptr_destroy(&rule->conditions);

	trx_vector_uint64_destroy(&rule->conditionids);

	trx_free(rule);
}

static trx_hash_t	template_item_hash_func(const void *d)
{
	const trx_template_item_t	*item = *(const trx_template_item_t **)d;

	return TRX_DEFAULT_UINT64_HASH_FUNC(&item->templateid);
}

static int	template_item_compare_func(const void *d1, const void *d2)
{
	const trx_template_item_t	*item1 = *(const trx_template_item_t **)d1;
	const trx_template_item_t	*item2 = *(const trx_template_item_t **)d2;

	TRX_RETURN_IF_NOT_EQUAL(item1->templateid, item2->templateid);
	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: copy_template_items_preproc                                      *
 *                                                                            *
 * Purpose: copy template item preprocessing options                          *
 *                                                                            *
 * Parameters: templateids - [IN] array of template IDs                       *
 *             items       - [IN] array of new/updated items                  *
 *                                                                            *
 ******************************************************************************/
static void	copy_template_items_preproc(const trx_vector_uint64_t *templateids, const trx_vector_ptr_t *items)
{
	trx_vector_uint64_t		itemids;
	trx_hashset_t			items_t;
	int				i;
	const trx_template_item_t	*item, **pitem;
	char				*sql = NULL;
	size_t				sql_alloc = 0, sql_offset = 0;
	DB_ROW				row;
	DB_RESULT			result;
	trx_db_insert_t			db_insert;

	if (0 == items->values_num)
		return;

	trx_vector_uint64_create(&itemids);
	trx_hashset_create(&items_t, items->values_num, template_item_hash_func, template_item_compare_func);

	/* remove old item preprocessing options */

	for (i = 0; i < items->values_num; i++)
	{
		item = (const trx_template_item_t *)items->values[i];

		if (NULL == item->key)
			trx_vector_uint64_append(&itemids, item->itemid);

		trx_hashset_insert(&items_t, &item, sizeof(trx_template_item_t *));
	}

	if (0 != itemids.values_num)
	{
		trx_vector_uint64_sort(&itemids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from item_preproc where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid", itemids.values, itemids.values_num);
		DBexecute("%s", sql);
		sql_offset = 0;
	}

	trx_db_insert_prepare(&db_insert, "item_preproc", "item_preprocid", "itemid", "step", "type", "params",
			"error_handler", "error_handler_params", NULL);

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select ip.itemid,ip.step,ip.type,ip.params,ip.error_handler,ip.error_handler_params"
				" from item_preproc ip,items ti"
				" where ip.itemid=ti.itemid"
				" and");

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "ti.hostid", templateids->values, templateids->values_num);

	result = DBselect("%s", sql);
	while (NULL != (row = DBfetch(result)))
	{
		trx_template_item_t	item_local, *pitem_local = &item_local;

		TRX_STR2UINT64(item_local.templateid, row[0]);
		if (NULL == (pitem = (const trx_template_item_t **)trx_hashset_search(&items_t, &pitem_local)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		trx_db_insert_add_values(&db_insert, __UINT64_C(0), (*pitem)->itemid, atoi(row[1]), atoi(row[2]),
				row[3], atoi(row[4]), row[5]);

	}
	DBfree_result(result);

	trx_db_insert_autoincrement(&db_insert, "item_preprocid");
	trx_db_insert_execute(&db_insert);
	trx_db_insert_clean(&db_insert);

	trx_free(sql);
	trx_hashset_destroy(&items_t);
	trx_vector_uint64_destroy(&itemids);
}

/******************************************************************************
 *                                                                            *
 * Function: copy_template_lld_macro_paths                                    *
 *                                                                            *
 * Purpose: copy template discovery item lld macro paths                      *
 *                                                                            *
 * Parameters: templateids - [IN] array of template IDs                       *
 *             items       - [IN] array of new/updated items                  *
 *                                                                            *
 ******************************************************************************/
static void	copy_template_lld_macro_paths(const trx_vector_uint64_t *templateids, const trx_vector_ptr_t *items)
{
	trx_vector_uint64_t		itemids;
	trx_hashset_t			items_t;
	int				i;
	const trx_template_item_t	*item, **pitem;
	char				*sql = NULL;
	size_t				sql_alloc = 0, sql_offset = 0;
	DB_ROW				row;
	DB_RESULT			result;
	trx_db_insert_t			db_insert;

	if (0 == items->values_num)
		return;

	trx_vector_uint64_create(&itemids);
	trx_hashset_create(&items_t, items->values_num, template_item_hash_func, template_item_compare_func);

	/* remove old lld rules macros */

	for (i = 0; i < items->values_num; i++)
	{
		item = (const trx_template_item_t *)items->values[i];

		if (0 == (TRX_FLAG_DISCOVERY_RULE & item->flags))
			continue;

		if (NULL == item->key)	/* item already existed */
			trx_vector_uint64_append(&itemids, item->itemid);

		trx_hashset_insert(&items_t, &item, sizeof(trx_template_item_t *));
	}

	if (0 != itemids.values_num)
	{
		trx_vector_uint64_sort(&itemids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from lld_macro_path where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid", itemids.values, itemids.values_num);
		DBexecute("%s", sql);
		sql_offset = 0;
	}

	trx_db_insert_prepare(&db_insert, "lld_macro_path", "lld_macro_pathid", "itemid", "lld_macro", "path", NULL);

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select l.itemid,l.lld_macro,l.path"
				" from lld_macro_path l,items i"
				" where l.itemid=i.itemid"
				" and");

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "i.hostid", templateids->values, templateids->values_num);
	result = DBselect("%s", sql);
	while (NULL != (row = DBfetch(result)))
	{
		trx_template_item_t	item_local, *pitem_local = &item_local;

		TRX_STR2UINT64(item_local.templateid, row[0]);
		if (NULL == (pitem = (const trx_template_item_t **)trx_hashset_search(&items_t, &pitem_local)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		trx_db_insert_add_values(&db_insert, __UINT64_C(0), (*pitem)->itemid, row[1], row[2]);

	}
	DBfree_result(result);

	trx_db_insert_autoincrement(&db_insert, "lld_macro_pathid");
	trx_db_insert_execute(&db_insert);
	trx_db_insert_clean(&db_insert);

	trx_free(sql);
	trx_hashset_destroy(&items_t);
	trx_vector_uint64_destroy(&itemids);
}

/******************************************************************************
 *                                                                            *
 * Function: compare_template_items                                           *
 *                                                                            *
 * Purpose: compare templateid of two template items                          *
 *                                                                            *
 * Parameters: d1 - [IN] first template item                                  *
 *             d2 - [IN] second template item                                 *
 *                                                                            *
 * Return value: compare result (-1 for d1<d2, 1 for d1>d2, 0 for d1==d2)     *
 *                                                                            *
 ******************************************************************************/
static int	compare_template_items(const void *d1, const void *d2)
{
	const trx_template_item_t	*i1 = *(const trx_template_item_t **)d1;
	const trx_template_item_t	*i2 = *(const trx_template_item_t **)d2;

	return trx_default_uint64_compare_func(&i1->templateid, &i2->templateid);
}

/******************************************************************************
 *                                                                            *
 * Function: link_template_dependent_items                                    *
 *                                                                            *
 * Purpose: create dependent item index in master item data                   *
 *                                                                            *
 * Parameters: items       - [IN/OUT] the template items                      *
 *                                                                            *
 ******************************************************************************/
static void	link_template_dependent_items(trx_vector_ptr_t *items)
{
	trx_template_item_t	*item, *master, item_local;
	int			i, index;
	trx_vector_ptr_t	template_index;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&template_index);
	trx_vector_ptr_append_array(&template_index, items->values, items->values_num);
	trx_vector_ptr_sort(&template_index, compare_template_items);

	for (i = items->values_num - 1; i >= 0; i--)
	{
		item = (trx_template_item_t *)items->values[i];
		if (0 != item->master_itemid)
		{
			item_local.templateid = item->master_itemid;
			if (FAIL == (index = trx_vector_ptr_bsearch(&template_index, &item_local,
					compare_template_items)))
			{
				/* dependent item without master item should be removed */
				THIS_SHOULD_NEVER_HAPPEN;
				free_template_item(item);
				trx_vector_ptr_remove(items, i);
			}
			else
			{
				master = (trx_template_item_t *)template_index.values[index];
				trx_vector_ptr_append(&master->dependent_items, item);
			}
		}
	}

	trx_vector_ptr_destroy(&template_index);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DBcopy_template_items                                            *
 *                                                                            *
 * Purpose: copy template items to host                                       *
 *                                                                            *
 * Parameters: hostid      - [IN] host id                                     *
 *             templateids - [IN] array of template IDs                       *
 *                                                                            *
 ******************************************************************************/
void	DBcopy_template_items(trx_uint64_t hostid, const trx_vector_uint64_t *templateids)
{
	trx_vector_ptr_t	items, lld_rules;
	int			new_conditions = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_ptr_create(&items);
	trx_vector_ptr_create(&lld_rules);

	get_template_items(hostid, templateids, &items);

	if (0 == items.values_num)
		goto out;

	get_template_lld_rule_map(&items, &lld_rules);

	new_conditions = calculate_template_lld_rule_conditionids(&lld_rules);
	update_template_lld_rule_formulas(&items, &lld_rules);

	link_template_dependent_items(&items);
	save_template_items(hostid, &items);
	save_template_lld_rules(&items, &lld_rules, new_conditions);
	save_template_item_applications(&items);
	save_template_discovery_prototypes(hostid, &items);
	copy_template_items_preproc(templateids, &items);
	copy_template_lld_macro_paths(templateids, &items);
out:
	trx_vector_ptr_clear_ext(&lld_rules, (trx_clean_func_t)free_lld_rule_map);
	trx_vector_ptr_destroy(&lld_rules);

	trx_vector_ptr_clear_ext(&items, (trx_clean_func_t)free_template_item);
	trx_vector_ptr_destroy(&items);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}
