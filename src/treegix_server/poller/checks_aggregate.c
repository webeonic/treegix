

#include "common.h"
#include "log.h"
#include "valuecache.h"
#include "dbcache.h"

#include "checks_aggregate.h"

#define TRX_VALUE_FUNC_MIN	0
#define TRX_VALUE_FUNC_AVG	1
#define TRX_VALUE_FUNC_MAX	2
#define TRX_VALUE_FUNC_SUM	3
#define TRX_VALUE_FUNC_COUNT	4
#define TRX_VALUE_FUNC_LAST	5

/******************************************************************************
 *                                                                            *
 * Function: evaluate_history_func_min                                        *
 *                                                                            *
 * Purpose: calculate minimum value from the history value vector             *
 *                                                                            *
 * Parameters: values      - [IN] a vector containing history values          *
 *             value_type  - [IN] the type of values. Only float/uint64       *
 *                           values are supported.                            *
 *             result      - [OUT] the resulting value                        *
 *                                                                            *
 ******************************************************************************/
static void	evaluate_history_func_min(trx_vector_history_record_t *values, int value_type, history_value_t *result)
{
	int	i;

	*result = values->values[0].value;

	if (ITEM_VALUE_TYPE_UINT64 == value_type)
	{
		for (i = 1; i < values->values_num; i++)
			if (values->values[i].value.ui64 < result->ui64)
				result->ui64 = values->values[i].value.ui64;
	}
	else
	{
		for (i = 1; i < values->values_num; i++)
			if (values->values[i].value.dbl < result->dbl)
				result->dbl = values->values[i].value.dbl;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_history_func_max                                        *
 *                                                                            *
 * Purpose: calculate maximum value from the history value vector             *
 *                                                                            *
 * Parameters: values      - [IN] a vector containing history values          *
 *             value_type  - [IN] the type of values. Only float/uint64       *
 *                           values are supported.                            *
 *             result      - [OUT] the resulting value                        *
 *                                                                            *
 ******************************************************************************/
static void	evaluate_history_func_max(trx_vector_history_record_t *values, int value_type, history_value_t *result)
{
	int	i;

	*result = values->values[0].value;

	if (ITEM_VALUE_TYPE_UINT64 == value_type)
	{
		for (i = 1; i < values->values_num; i++)
			if (values->values[i].value.ui64 > result->ui64)
				result->ui64 = values->values[i].value.ui64;
	}
	else
	{
		for (i = 1; i < values->values_num; i++)
			if (values->values[i].value.dbl > result->dbl)
				result->dbl = values->values[i].value.dbl;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_history_func_sum                                        *
 *                                                                            *
 * Purpose: calculate sum of values from the history value vector             *
 *                                                                            *
 * Parameters: values      - [IN] a vector containing history values          *
 *             value_type  - [IN] the type of values. Only float/uint64       *
 *                           values are supported.                            *
 *             result      - [OUT] the resulting value                        *
 *                                                                            *
 ******************************************************************************/
static void	evaluate_history_func_sum(trx_vector_history_record_t *values, int value_type, history_value_t *result)
{
	int	i;

	if (ITEM_VALUE_TYPE_UINT64 == value_type)
	{
		result->ui64 = 0;
		for (i = 0; i < values->values_num; i++)
			result->ui64 += values->values[i].value.ui64;
	}
	else
	{
		result->dbl = 0;
		for (i = 0; i < values->values_num; i++)
			result->dbl += values->values[i].value.dbl;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_history_func_avg                                        *
 *                                                                            *
 * Purpose: calculate average value of values from the history value vector   *
 *                                                                            *
 * Parameters: values      - [IN] a vector containing history values          *
 *             value_type  - [IN] the type of values. Only float/uint64       *
 *                           values are supported.                            *
 *             result      - [OUT] the resulting value                        *
 *                                                                            *
 ******************************************************************************/
static void	evaluate_history_func_avg(trx_vector_history_record_t *values, int value_type, history_value_t *result)
{
	evaluate_history_func_sum(values, value_type, result);

	if (ITEM_VALUE_TYPE_UINT64 == value_type)
		result->ui64 /= values->values_num;
	else
		result->dbl /= values->values_num;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_history_func_count                                      *
 *                                                                            *
 * Purpose: calculate number of values in value vector                        *
 *                                                                            *
 * Parameters: values      - [IN] a vector containing history values          *
 *             value_type  - [IN] the type of values. Only float/uint64       *
 *                           values are supported.                            *
 *             result      - [OUT] the resulting value                        *
 *                                                                            *
 ******************************************************************************/
static void	evaluate_history_func_count(trx_vector_history_record_t *values, int value_type,
		history_value_t *result)
{
	if (ITEM_VALUE_TYPE_UINT64 == value_type)
		result->ui64 = values->values_num;
	else
		result->dbl = values->values_num;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_history_func_last                                       *
 *                                                                            *
 * Purpose: calculate the last (newest) value in value vector                 *
 *                                                                            *
 * Parameters: values      - [IN] a vector containing history values          *
 *             result      - [OUT] the resulting value                        *
 *                                                                            *
 ******************************************************************************/
static void	evaluate_history_func_last(trx_vector_history_record_t *values, history_value_t *result)
{
	*result = values->values[0].value;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_history_func                                            *
 *                                                                            *
 * Purpose: calculate function with values from value vector                  *
 *                                                                            *
 * Parameters: values      - [IN] a vector containing history values          *
 *             value_type  - [IN] the type of values. Only float/uint64       *
 *                           values are supported.                            *
 *             func        - [IN] the function to calculate. Only             *
 *                           TRX_VALUE_FUNC_MIN, TRX_VALUE_FUNC_AVG,          *
 *                           TRX_VALUE_FUNC_MAX, TRX_VALUE_FUNC_SUM,          *
 *                           TRX_VALUE_FUNC_COUNT, TRX_VALUE_FUNC_LAST        *
 *                           functions are supported.                         *
 *             result      - [OUT] the resulting value                        *
 *                                                                            *
 ******************************************************************************/
static void	evaluate_history_func(trx_vector_history_record_t *values, int value_type, int func,
		history_value_t *result)
{
	switch (func)
	{
		case TRX_VALUE_FUNC_MIN:
			evaluate_history_func_min(values, value_type, result);
			break;
		case TRX_VALUE_FUNC_AVG:
			evaluate_history_func_avg(values, value_type, result);
			break;
		case TRX_VALUE_FUNC_MAX:
			evaluate_history_func_max(values, value_type, result);
			break;
		case TRX_VALUE_FUNC_SUM:
			evaluate_history_func_sum(values, value_type, result);
			break;
		case TRX_VALUE_FUNC_COUNT:
			evaluate_history_func_count(values, value_type, result);
			break;
		case TRX_VALUE_FUNC_LAST:
			evaluate_history_func_last(values, result);
			break;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: quote_string                                                     *
 *                                                                            *
 * Purpose: quotes string by enclosing it in double quotes and escaping       *
 *          double quotes inside string with '\'.                             *
 *                                                                            *
 * Parameters: str    - [IN/OUT] the string to quote                          *
 *             sz_str - [IN] the string length                                *
 *                                                                            *
 * Comments: The '\' character itself is not quoted. As the result if string  *
 *           ends with '\' it can be quoted (for example for error messages), *
 *           but it's impossible to unquote it.                               *
 *                                                                            *
 ******************************************************************************/
static void	quote_string(char **str, size_t sz_src)
{
	size_t	sz_dst;

	sz_dst = trx_get_escape_string_len(*str, "\"") + 3;

	*str = (char *)trx_realloc(*str, sz_dst);

	(*str)[--sz_dst] = '\0';
	(*str)[--sz_dst] = '"';

	while (0 < sz_src)
	{
		(*str)[--sz_dst] = (*str)[--sz_src];

		if ('"' == (*str)[sz_src])
			(*str)[--sz_dst] = '\\';
	}
	(*str)[--sz_dst] = '"';
}

/******************************************************************************
 *                                                                            *
 * Function: aggregate_quote_groups                                           *
 *                                                                            *
 * Purpose: quotes the individual groups in the list if necessary             *
 *                                                                            *
 ******************************************************************************/
static void	aggregate_quote_groups(char **str, size_t *str_alloc, size_t *str_offset, const char *groups)
{
	int	i, num;
	char	*group, *separator = "";

	num = num_param(groups);

	for (i = 1; i <= num; i++)
	{
		if (NULL == (group = get_param_dyn(groups, i)))
			continue;

		trx_strcpy_alloc(str, str_alloc, str_offset, separator);
		separator = (char *)", ";

		quote_string(&group, strlen(group));
		trx_strcpy_alloc(str, str_alloc, str_offset, group);
		trx_free(group);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: aggregate_get_items                                              *
 *                                                                            *
 * Purpose: get array of items specified by key for selected groups           *
 *          (including nested groups)                                         *
 *                                                                            *
 * Parameters: itemids - [OUT] list of item ids                               *
 *             groups  - [IN] list of comma-separated host groups             *
 *             itemkey - [IN] item key to aggregate                           *
 *             error   - [OUT] the error message                              *
 *                                                                            *
 * Return value: SUCCEED - item identifier(s) were retrieved successfully     *
 *               FAIL    - no items matching the specified groups or keys     *
 *                                                                            *
 ******************************************************************************/
static int	aggregate_get_items(trx_vector_uint64_t *itemids, const char *groups, const char *itemkey, char **error)
{
	char			*group, *esc;
	DB_RESULT		result;
	DB_ROW			row;
	trx_uint64_t		itemid;
	char			*sql = NULL;
	size_t			sql_alloc = TRX_KIBIBYTE, sql_offset = 0, error_alloc = 0, error_offset = 0;
	int			num, n, ret = FAIL;
	trx_vector_uint64_t	groupids;
	trx_vector_str_t	group_names;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() groups:'%s' itemkey:'%s'", __func__, groups, itemkey);

	trx_vector_uint64_create(&groupids);
	trx_vector_str_create(&group_names);

	num = num_param(groups);
	for (n = 1; n <= num; n++)
	{
		if (NULL == (group = get_param_dyn(groups, n)))
			continue;

		trx_vector_str_append(&group_names, group);
	}

	trx_dc_get_nested_hostgroupids_by_names(group_names.values, group_names.values_num, &groupids);
	trx_vector_str_clear_ext(&group_names, trx_str_free);
	trx_vector_str_destroy(&group_names);

	if (0 == groupids.values_num)
	{
		trx_strcpy_alloc(error, &error_alloc, &error_offset, "None of the groups in list ");
		aggregate_quote_groups(error, &error_alloc, &error_offset, groups);
		trx_strcpy_alloc(error, &error_alloc, &error_offset, " is correct.");
		goto out;
	}

	sql = (char *)trx_malloc(sql, sql_alloc);
	esc = DBdyn_escape_string(itemkey);

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select distinct i.itemid"
			" from items i,hosts h,hosts_groups hg,item_rtdata ir"
			" where i.hostid=h.hostid"
				" and h.hostid=hg.hostid"
				" and i.key_='%s'"
				" and i.status=%d"
				" and ir.itemid=i.itemid"
				" and ir.state=%d"
				" and h.status=%d"
				" and",
			esc, ITEM_STATUS_ACTIVE, ITEM_STATE_NORMAL, HOST_STATUS_MONITORED);

	trx_free(esc);

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hg.groupid", groupids.values, groupids.values_num);
	result = DBselect("%s", sql);
	trx_free(sql);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(itemid, row[0]);
		trx_vector_uint64_append(itemids, itemid);
	}
	DBfree_result(result);

	if (0 == itemids->values_num)
	{
		trx_snprintf_alloc(error, &error_alloc, &error_offset, "No items for key \"%s\" in group(s) ", itemkey);
		aggregate_quote_groups(error, &error_alloc, &error_offset, groups);
		trx_chrcpy_alloc(error, &error_alloc, &error_offset, '.');
		goto out;
	}

	trx_vector_uint64_sort(itemids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	ret = SUCCEED;

out:
	trx_vector_uint64_destroy(&groupids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_aggregate                                               *
 *                                                                            *
 * Parameters: item      - [IN] aggregated item                               *
 *             grp_func  - [IN] one of TRX_GRP_FUNC_*                         *
 *             groups    - [IN] list of comma-separated host groups           *
 *             itemkey   - [IN] item key to aggregate                         *
 *             item_func - [IN] one of TRX_VALUE_FUNC_*                       *
 *             param     - [IN] item_func parameter (optional)                *
 *                                                                            *
 * Return value: SUCCEED - aggregate item evaluated successfully              *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_aggregate(DC_ITEM *item, AGENT_RESULT *res, int grp_func, const char *groups,
		const char *itemkey, int item_func, const char *param)
{
	trx_vector_uint64_t		itemids;
	history_value_t			value, item_result;
	trx_history_record_t		group_value;
	int				ret = FAIL, *errcodes = NULL, i, count, seconds;
	DC_ITEM				*items = NULL;
	trx_vector_history_record_t	values, group_values;
	char				*error = NULL;
	trx_timespec_t			ts;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() grp_func:%d groups:'%s' itemkey:'%s' item_func:%d param:'%s'",
			__func__, grp_func, groups, itemkey, item_func, TRX_NULL2STR(param));

	trx_timespec(&ts);

	trx_vector_uint64_create(&itemids);
	if (FAIL == aggregate_get_items(&itemids, groups, itemkey, &error))
	{
		SET_MSG_RESULT(res, error);
		goto clean1;
	}

	memset(&value, 0, sizeof(value));
	trx_history_record_vector_create(&group_values);

	items = (DC_ITEM *)trx_malloc(items, sizeof(DC_ITEM) * itemids.values_num);
	errcodes = (int *)trx_malloc(errcodes, sizeof(int) * itemids.values_num);

	DCconfig_get_items_by_itemids(items, itemids.values, errcodes, itemids.values_num);

	if (TRX_VALUE_FUNC_LAST == item_func)
	{
		count = 1;
		seconds = 0;
	}
	else
	{
		if (FAIL == is_time_suffix(param, &seconds, TRX_LENGTH_UNLIMITED))
		{
			SET_MSG_RESULT(res, trx_strdup(NULL, "Invalid fourth parameter."));
			goto clean2;
		}
		count = 0;
	}

	for (i = 0; i < itemids.values_num; i++)
	{
		if (SUCCEED != errcodes[i])
			continue;

		if (ITEM_STATUS_ACTIVE != items[i].status)
			continue;

		if (HOST_STATUS_MONITORED != items[i].host.status)
			continue;

		if (ITEM_VALUE_TYPE_FLOAT != items[i].value_type && ITEM_VALUE_TYPE_UINT64 != items[i].value_type)
			continue;

		trx_history_record_vector_create(&values);

		if (SUCCEED == trx_vc_get_values(items[i].itemid, items[i].value_type, &values, seconds, count, &ts) &&
				0 < values.values_num)
		{
			evaluate_history_func(&values, items[i].value_type, item_func, &item_result);

			if (item->value_type == items[i].value_type)
				group_value.value = item_result;
			else
			{
				if (ITEM_VALUE_TYPE_UINT64 == item->value_type)
					group_value.value.ui64 = (trx_uint64_t)item_result.dbl;
				else
					group_value.value.dbl = (double)item_result.ui64;
			}

			trx_vector_history_record_append_ptr(&group_values, &group_value);
		}

		trx_history_record_vector_destroy(&values, items[i].value_type);
	}

	if (0 == group_values.values_num)
	{
		char	*tmp = NULL;
		size_t	tmp_alloc = 0, tmp_offset = 0;

		aggregate_quote_groups(&tmp, &tmp_alloc, &tmp_offset, groups);
		SET_MSG_RESULT(res, trx_dsprintf(NULL, "No values for key \"%s\" in group(s) %s.", itemkey, tmp));
		trx_free(tmp);

		goto clean2;
	}

	evaluate_history_func(&group_values, item->value_type, grp_func, &value);

	if (ITEM_VALUE_TYPE_FLOAT == item->value_type)
		SET_DBL_RESULT(res, value.dbl);
	else
		SET_UI64_RESULT(res, value.ui64);

	ret = SUCCEED;
clean2:
	DCconfig_clean_items(items, errcodes, itemids.values_num);

	trx_free(errcodes);
	trx_free(items);
	trx_history_record_vector_destroy(&group_values, item->value_type);
clean1:
	trx_vector_uint64_destroy(&itemids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: get_value_aggregate                                              *
 *                                                                            *
 * Purpose: retrieve data from Treegix server (aggregate items)                *
 *                                                                            *
 * Parameters: item - item we are interested in                               *
 *                                                                            *
 * Return value: SUCCEED - data successfully retrieved and stored in result   *
 *                         and result_str (as string)                         *
 *               NOTSUPPORTED - requested item is not supported               *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
int	get_value_aggregate(DC_ITEM *item, AGENT_RESULT *result)
{
	AGENT_REQUEST	request;
	int		ret = NOTSUPPORTED;
	const char	*tmp, *groups, *itemkey, *funcp = NULL;
	int		grp_func, item_func, params_num;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() key:'%s'", __func__, item->key_orig);

	init_request(&request);

	if (ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Value type must be Numeric for aggregate items"));
		goto out;
	}

	if (SUCCEED != parse_item_key(item->key, &request))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid item key format."));
		goto out;
	}

	if (0 == strcmp(get_rkey(&request), "grpmin"))
	{
		grp_func = TRX_VALUE_FUNC_MIN;
	}
	else if (0 == strcmp(get_rkey(&request), "grpavg"))
	{
		grp_func = TRX_VALUE_FUNC_AVG;
	}
	else if (0 == strcmp(get_rkey(&request), "grpmax"))
	{
		grp_func = TRX_VALUE_FUNC_MAX;
	}
	else if (0 == strcmp(get_rkey(&request), "grpsum"))
	{
		grp_func = TRX_VALUE_FUNC_SUM;
	}
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid item key."));
		goto out;
	}

	params_num = get_rparams_num(&request);

	if (3 > params_num || params_num > 4)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
		goto out;
	}

	groups = get_rparam(&request, 0);
	itemkey = get_rparam(&request, 1);
	tmp = get_rparam(&request, 2);

	if (0 == strcmp(tmp, "min"))
		item_func = TRX_VALUE_FUNC_MIN;
	else if (0 == strcmp(tmp, "avg"))
		item_func = TRX_VALUE_FUNC_AVG;
	else if (0 == strcmp(tmp, "max"))
		item_func = TRX_VALUE_FUNC_MAX;
	else if (0 == strcmp(tmp, "sum"))
		item_func = TRX_VALUE_FUNC_SUM;
	else if (0 == strcmp(tmp, "count"))
		item_func = TRX_VALUE_FUNC_COUNT;
	else if (0 == strcmp(tmp, "last"))
		item_func = TRX_VALUE_FUNC_LAST;
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid third parameter."));
		goto out;
	}

	if (4 == params_num)
	{
		funcp = get_rparam(&request, 3);
	}
	else if (3 == params_num && TRX_VALUE_FUNC_LAST != item_func)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
		goto out;
	}

	if (SUCCEED != evaluate_aggregate(item, result, grp_func, groups, itemkey, item_func, funcp))
		goto out;

	ret = SUCCEED;
out:
	free_request(&request);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}
