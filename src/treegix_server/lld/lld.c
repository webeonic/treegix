

#include "lld.h"
#include "db.h"
#include "log.h"
#include "../events.h"
#include "trxalgo.h"
#include "trxserver.h"
#include "trxregexp.h"
#include "proxy.h"

/* lld rule filter condition (item_condition table record) */
typedef struct
{
	trx_uint64_t		id;
	char			*macro;
	char			*regexp;
	trx_vector_ptr_t	regexps;
	unsigned char		op;
}
lld_condition_t;

/* lld rule filter */
typedef struct
{
	trx_vector_ptr_t	conditions;
	char			*expression;
	int			evaltype;
}
lld_filter_t;

/******************************************************************************
 *                                                                            *
 * Function: lld_condition_free                                               *
 *                                                                            *
 * Purpose: release resources allocated by filter condition                   *
 *                                                                            *
 * Parameters: condition  - [IN] the filter condition                         *
 *                                                                            *
 ******************************************************************************/
static void	lld_condition_free(lld_condition_t *condition)
{
	trx_regexp_clean_expressions(&condition->regexps);
	trx_vector_ptr_destroy(&condition->regexps);

	trx_free(condition->macro);
	trx_free(condition->regexp);
	trx_free(condition);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_conditions_free                                              *
 *                                                                            *
 * Purpose: release resources allocated by filter conditions                  *
 *                                                                            *
 * Parameters: conditions - [IN] the filter conditions                        *
 *                                                                            *
 ******************************************************************************/
static void	lld_conditions_free(trx_vector_ptr_t *conditions)
{
	trx_vector_ptr_clear_ext(conditions, (trx_clean_func_t)lld_condition_free);
	trx_vector_ptr_destroy(conditions);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_condition_compare_by_macro                                   *
 *                                                                            *
 * Purpose: compare two filter conditions by their macros                     *
 *                                                                            *
 * Parameters: item1  - [IN] the first filter condition                       *
 *             item2  - [IN] the second filter condition                      *
 *                                                                            *
 ******************************************************************************/
static int	lld_condition_compare_by_macro(const void *item1, const void *item2)
{
	lld_condition_t	*condition1 = *(lld_condition_t **)item1;
	lld_condition_t	*condition2 = *(lld_condition_t **)item2;

	return strcmp(condition1->macro, condition2->macro);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_filter_init                                                  *
 *                                                                            *
 * Purpose: initializes lld filter                                            *
 *                                                                            *
 * Parameters: filter  - [IN] the lld filter                                  *
 *                                                                            *
 ******************************************************************************/
static void	lld_filter_init(lld_filter_t *filter)
{
	trx_vector_ptr_create(&filter->conditions);
	filter->expression = NULL;
	filter->evaltype = CONDITION_EVAL_TYPE_AND_OR;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_filter_clean                                                 *
 *                                                                            *
 * Purpose: releases resources allocated by lld filter                        *
 *                                                                            *
 * Parameters: filter  - [IN] the lld filter                                  *
 *                                                                            *
 ******************************************************************************/
static void	lld_filter_clean(lld_filter_t *filter)
{
	trx_free(filter->expression);
	lld_conditions_free(&filter->conditions);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_filter_load                                                  *
 *                                                                            *
 * Purpose: loads lld filter data                                             *
 *                                                                            *
 * Parameters: filter     - [IN] the lld filter                               *
 *             lld_ruleid - [IN] the lld rule id                              *
 *             error      - [OUT] the error description                       *
 *                                                                            *
 ******************************************************************************/
static int	lld_filter_load(lld_filter_t *filter, trx_uint64_t lld_ruleid, char **error)
{
	DB_RESULT	result;
	DB_ROW		row;
	lld_condition_t	*condition;
	DC_ITEM		item;
	int		errcode, ret = SUCCEED;

	DCconfig_get_items_by_itemids(&item, &lld_ruleid, &errcode, 1);

	if (SUCCEED != errcode)
	{
		*error = trx_dsprintf(*error, "Invalid discovery rule ID [" TRX_FS_UI64 "].",
				lld_ruleid);
		ret = FAIL;
		goto out;
	}

	result = DBselect(
			"select item_conditionid,macro,value,operator"
			" from item_condition"
			" where itemid=" TRX_FS_UI64,
			lld_ruleid);

	while (NULL != (row = DBfetch(result)))
	{
		condition = (lld_condition_t *)trx_malloc(NULL, sizeof(lld_condition_t));
		TRX_STR2UINT64(condition->id, row[0]);
		condition->macro = trx_strdup(NULL, row[1]);
		condition->regexp = trx_strdup(NULL, row[2]);
		condition->op = (unsigned char)atoi(row[3]);

		trx_vector_ptr_create(&condition->regexps);

		trx_vector_ptr_append(&filter->conditions, condition);

		if ('@' == *condition->regexp)
		{
			DCget_expressions_by_name(&condition->regexps, condition->regexp + 1);

			if (0 == condition->regexps.values_num)
			{
				*error = trx_dsprintf(*error, "Global regular expression \"%s\" does not exist.",
						condition->regexp + 1);
				ret = FAIL;
				break;
			}
		}
		else
		{
			substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, &item, NULL, NULL,
					&condition->regexp, MACRO_TYPE_LLD_FILTER, NULL, 0);
		}
	}
	DBfree_result(result);

	if (SUCCEED != ret)
		lld_conditions_free(&filter->conditions);
	else if (CONDITION_EVAL_TYPE_AND_OR == filter->evaltype)
		trx_vector_ptr_sort(&filter->conditions, lld_condition_compare_by_macro);
out:
	DCconfig_clean_items(&item, &errcode, 1);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: filter_condition_match                                           *
 *                                                                            *
 * Purpose: check if the lld data passes filter evaluation                    *
 *                                                                            *
 * Parameters: filter          - [IN] the lld filter                          *
 *             jp_row          - [IN] the lld data row                        *
 *             lld_macro_paths - [IN] use json path to extract from jp_row    *
 *                                                                            *
 * Return value: SUCCEED - the lld data passed filter evaluation              *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	filter_condition_match(const struct trx_json_parse *jp_row, const trx_vector_ptr_t *lld_macro_paths,
		const lld_condition_t *condition)
{
	char	*value = NULL;
	int	ret;

	if (SUCCEED == (ret = trx_lld_macro_value_by_name(jp_row, lld_macro_paths, condition->macro, &value)))
	{
		switch (regexp_match_ex(&condition->regexps, value, condition->regexp, TRX_CASE_SENSITIVE))
		{
			case TRX_REGEXP_MATCH:
				ret = (CONDITION_OPERATOR_REGEXP == condition->op ? SUCCEED : FAIL);
				break;
			case TRX_REGEXP_NO_MATCH:
				ret = (CONDITION_OPERATOR_NOT_REGEXP == condition->op ? SUCCEED : FAIL);
				break;
			default:
				ret = FAIL;
		}
	}

	trx_free(value);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: filter_evaluate_and_or                                           *
 *                                                                            *
 * Purpose: check if the lld data passes filter evaluation by and/or rule     *
 *                                                                            *
 * Parameters: filter          - [IN] the lld filter                          *
 *             jp_row          - [IN] the lld data row                        *
 *             lld_macro_paths - [IN] use json path to extract from jp_row    *
 *                                                                            *
 * Return value: SUCCEED - the lld data passed filter evaluation              *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	filter_evaluate_and_or(const lld_filter_t *filter, const struct trx_json_parse *jp_row,
		const trx_vector_ptr_t *lld_macro_paths)
{
	int	i, ret = SUCCEED, rc = SUCCEED;
	char	*lastmacro = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (i = 0; i < filter->conditions.values_num; i++)
	{
		const lld_condition_t	*condition = (lld_condition_t *)filter->conditions.values[i];

		rc = filter_condition_match(jp_row, lld_macro_paths, condition);
		/* check if a new condition group has started */
		if (NULL == lastmacro || 0 != strcmp(lastmacro, condition->macro))
		{
			/* if any of condition groups are false the evaluation returns false */
			if (FAIL == ret)
				break;

			ret = rc;
		}
		else
		{
			if (SUCCEED == rc)
				ret = rc;
		}

		lastmacro = condition->macro;
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: filter_evaluate_and                                              *
 *                                                                            *
 * Purpose: check if the lld data passes filter evaluation by and rule        *
 *                                                                            *
 * Parameters: filter          - [IN] the lld filter                          *
 *             jp_row          - [IN] the lld data row                        *
 *             lld_macro_paths - [IN] use json path to extract from jp_row    *
 *                                                                            *
 * Return value: SUCCEED - the lld data passed filter evaluation              *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	filter_evaluate_and(const lld_filter_t *filter, const struct trx_json_parse *jp_row,
		const trx_vector_ptr_t *lld_macro_paths)
{
	int	i, ret = SUCCEED;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (i = 0; i < filter->conditions.values_num; i++)
	{
		/* if any of conditions are false the evaluation returns false */
		if (SUCCEED != (ret = filter_condition_match(jp_row, lld_macro_paths,
				(lld_condition_t *)filter->conditions.values[i])))
		{
			break;
		}
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: filter_evaluate_or                                               *
 *                                                                            *
 * Purpose: check if the lld data passes filter evaluation by or rule         *
 *                                                                            *
 * Parameters: filter          - [IN] the lld filter                          *
 *             jp_row          - [IN] the lld data row                        *
 *             lld_macro_paths - [IN] use json path to extract from jp_row    *
 *                                                                            *
 * Return value: SUCCEED - the lld data passed filter evaluation              *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	filter_evaluate_or(const lld_filter_t *filter, const struct trx_json_parse *jp_row,
		const trx_vector_ptr_t *lld_macro_paths)
{
	int	i, ret = SUCCEED;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (i = 0; i < filter->conditions.values_num; i++)
	{
		/* if any of conditions are true the evaluation returns true */
		if (SUCCEED == (ret = filter_condition_match(jp_row, lld_macro_paths,
				(lld_condition_t *)filter->conditions.values[i])))
		{
			break;
		}
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: filter_evaluate_expression                                       *
 *                                                                            *
 * Purpose: check if the lld data passes filter evaluation by custom          *
 *          expression                                                        *
 *                                                                            *
 * Parameters: filter          - [IN] the lld filter                          *
 *             jp_row          - [IN] the lld data row                        *
 *             lld_macro_paths - [IN] use json path to extract from jp_row    *
 *                                                                            *
 * Return value: SUCCEED - the lld data passed filter evaluation              *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: 1) replace {item_condition} references with action condition     *
 *              evaluation results (1 or 0)                                   *
 *           2) call evaluate() to calculate the final result                 *
 *                                                                            *
 ******************************************************************************/
static int	filter_evaluate_expression(const lld_filter_t *filter, const struct trx_json_parse *jp_row,
		const trx_vector_ptr_t *lld_macro_paths)
{
	int	i, ret = FAIL, id_len;
	char	*expression, id[TRX_MAX_UINT64_LEN + 2], *p, error[256];
	double	result;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() expression:%s", __func__, filter->expression);

	expression = trx_strdup(NULL, filter->expression);

	for (i = 0; i < filter->conditions.values_num; i++)
	{
		const lld_condition_t	*condition = (lld_condition_t *)filter->conditions.values[i];

		ret = filter_condition_match(jp_row, lld_macro_paths, condition);

		trx_snprintf(id, sizeof(id), "{" TRX_FS_UI64 "}", condition->id);

		id_len = strlen(id);
		p = expression;

		while (NULL != (p = strstr(p, id)))
		{
			*p = (SUCCEED == ret ? '1' : '0');
			memset(p + 1, ' ', id_len - 1);
			p += id_len;
		}
	}

	if (SUCCEED == evaluate(&result, expression, error, sizeof(error), NULL))
		ret = (SUCCEED != trx_double_compare(result, 0) ? SUCCEED : FAIL);

	trx_free(expression);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: filter_evaluate                                                  *
 *                                                                            *
 * Purpose: check if the lld data passes filter evaluation                    *
 *                                                                            *
 * Parameters: filter          - [IN] the lld filter                          *
 *             jp_row          - [IN] the lld data row                        *
 *             lld_macro_paths - [IN] use json path to extract from jp_row    *
 *                                                                            *
 * Return value: SUCCEED - the lld data passed filter evaluation              *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	filter_evaluate(const lld_filter_t *filter, const struct trx_json_parse *jp_row,
		const trx_vector_ptr_t *lld_macro_paths)
{
	switch (filter->evaltype)
	{
		case CONDITION_EVAL_TYPE_AND_OR:
			return filter_evaluate_and_or(filter, jp_row, lld_macro_paths);
		case CONDITION_EVAL_TYPE_AND:
			return filter_evaluate_and(filter, jp_row, lld_macro_paths);
		case CONDITION_EVAL_TYPE_OR:
			return filter_evaluate_or(filter, jp_row, lld_macro_paths);
		case CONDITION_EVAL_TYPE_EXPRESSION:
			return filter_evaluate_expression(filter, jp_row, lld_macro_paths);
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_check_received_data_for_filter                               *
 *                                                                            *
 * Purpose: Check if the LLD data contains a values for macros used in filter.*
 *          Create an informative warning for every macro that has not        *
 *          received any value.                                               *
 *                                                                            *
 * Parameters: filter          - [IN] the lld filter                          *
 *             jp_row          - [IN] the lld data row                        *
 *             lld_macro_paths - [IN] use json path to extract from jp_row    *
 *             info            - [OUT] the warning description                *
 *                                                                            *
 ******************************************************************************/
static void	lld_check_received_data_for_filter(lld_filter_t *filter, const struct trx_json_parse *jp_row,
		const trx_vector_ptr_t *lld_macro_paths, char **info)
{
	int			i, index;
	trx_lld_macro_path_t	lld_macro_path_local, *lld_macro_path;
	char			*output = NULL;

	for (i = 0; i < filter->conditions.values_num; i++)
	{
		const lld_condition_t	*condition = (lld_condition_t *)filter->conditions.values[i];

		lld_macro_path_local.lld_macro = condition->macro;

		if (FAIL != (index = trx_vector_ptr_bsearch(lld_macro_paths, &lld_macro_path_local,
				trx_lld_macro_paths_compare)))
		{
			lld_macro_path = (trx_lld_macro_path_t *)lld_macro_paths->values[index];

			if (FAIL == trx_jsonpath_query(jp_row, lld_macro_path->path, &output) || NULL == output)
			{
				*info = trx_strdcatf(*info,
						"Cannot accurately apply filter: no value received for macro \"%s\""
						" json path '%s'.\n", lld_macro_path->lld_macro, lld_macro_path->path);
			}
			trx_free(output);

			continue;
		}

		if (NULL == trx_json_pair_by_name(jp_row, condition->macro))
		{
			*info = trx_strdcatf(*info,
					"Cannot accurately apply filter: no value received for macro \"%s\".\n",
					condition->macro);
		}
	}
}

static int	lld_rows_get(const char *value, lld_filter_t *filter, trx_vector_ptr_t *lld_rows,
		const trx_vector_ptr_t *lld_macro_paths, char **info, char **error)
{
	struct trx_json_parse	jp, jp_array, jp_row;
	const char		*p;
	trx_lld_row_t		*lld_row;
	int			ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != trx_json_open(value, &jp))
	{
		*error = trx_dsprintf(*error, "Invalid discovery rule value: %s", trx_json_strerror());
		goto out;
	}

	if ('[' == *jp.start)
	{
		jp_array = jp;
	}
	else if (SUCCEED != trx_json_brackets_by_name(&jp, TRX_PROTO_TAG_DATA, &jp_array))	/* deprecated */
	{
		*error = trx_dsprintf(*error, "Cannot find the \"%s\" array in the received JSON object.",
				TRX_PROTO_TAG_DATA);
		goto out;
	}

	p = NULL;
	while (NULL != (p = trx_json_next(&jp_array, p)))
	{
		if (FAIL == trx_json_brackets_open(p, &jp_row))
			continue;

		lld_check_received_data_for_filter(filter, &jp_row, lld_macro_paths, info);

		if (SUCCEED != filter_evaluate(filter, &jp_row, lld_macro_paths))
			continue;

		lld_row = (trx_lld_row_t *)trx_malloc(NULL, sizeof(trx_lld_row_t));
		lld_row->jp_row = jp_row;
		trx_vector_ptr_create(&lld_row->item_links);

		trx_vector_ptr_append(lld_rows, lld_row);
	}

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

static void	lld_item_link_free(trx_lld_item_link_t *item_link)
{
	trx_free(item_link);
}

static void	lld_row_free(trx_lld_row_t *lld_row)
{
	trx_vector_ptr_clear_ext(&lld_row->item_links, (trx_clean_func_t)lld_item_link_free);
	trx_vector_ptr_destroy(&lld_row->item_links);
	trx_free(lld_row);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_process_discovery_rule                                       *
 *                                                                            *
 * Purpose: add or update items, triggers and graphs for discovery item       *
 *                                                                            *
 * Parameters: lld_ruleid - [IN] discovery item identifier from database      *
 *             value      - [IN] received value from agent                    *
 *             error      - [OUT] error or informational message. Will be set *
 *                               to empty string on successful discovery      *
 *                               without additional information.              *
 *                                                                            *
 ******************************************************************************/
int	lld_process_discovery_rule(trx_uint64_t lld_ruleid, const char *value, char **error)
{
	DB_RESULT		result;
	DB_ROW			row;
	trx_uint64_t		hostid;
	char			*discovery_key = NULL, *info = NULL;
	int			lifetime, ret = SUCCEED;
	trx_vector_ptr_t	lld_rows, lld_macro_paths;
	lld_filter_t		filter;
	time_t			now;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() itemid:" TRX_FS_UI64, __func__, lld_ruleid);

	trx_vector_ptr_create(&lld_rows);
	trx_vector_ptr_create(&lld_macro_paths);

	lld_filter_init(&filter);

	result = DBselect(
			"select hostid,key_,evaltype,formula,lifetime"
			" from items"
			" where itemid=" TRX_FS_UI64,
			lld_ruleid);

	if (NULL != (row = DBfetch(result)))
	{
		char	*lifetime_str;

		TRX_STR2UINT64(hostid, row[0]);
		discovery_key = trx_strdup(discovery_key, row[1]);
		filter.evaltype = atoi(row[2]);
		filter.expression = trx_strdup(NULL, row[3]);
		lifetime_str = trx_strdup(NULL, row[4]);
		substitute_simple_macros(NULL, NULL, NULL, NULL, &hostid, NULL, NULL, NULL, NULL,
				&lifetime_str, MACRO_TYPE_COMMON, NULL, 0);

		if (SUCCEED != is_time_suffix(lifetime_str, &lifetime, TRX_LENGTH_UNLIMITED))
		{
			treegix_log(LOG_LEVEL_WARNING, "cannot process lost resources for the discovery rule \"%s:%s\":"
					" \"%s\" is not a valid value",
					trx_host_string(hostid), discovery_key, lifetime_str);
			lifetime = 25 * SEC_PER_YEAR;	/* max value for the field */
		}

		trx_free(lifetime_str);
	}
	DBfree_result(result);

	if (NULL == row)
	{
		treegix_log(LOG_LEVEL_WARNING, "invalid discovery rule ID [" TRX_FS_UI64 "]", lld_ruleid);
		goto out;
	}

	if (SUCCEED != lld_filter_load(&filter, lld_ruleid, error))
	{
		ret = FAIL;
		goto out;
	}

	if (SUCCEED != trx_lld_macro_paths_get(lld_ruleid, &lld_macro_paths, error))
	{
		ret = FAIL;
		goto out;
	}

	if (SUCCEED != lld_rows_get(value, &filter, &lld_rows, &lld_macro_paths, &info, error))
	{
		ret = FAIL;
		goto out;
	}

	*error = trx_strdup(*error, "");

	now = time(NULL);

	if (SUCCEED != lld_update_items(hostid, lld_ruleid, &lld_rows, &lld_macro_paths, error, lifetime, now))
	{
		treegix_log(LOG_LEVEL_DEBUG, "cannot update/add items because parent host was removed while"
				" processing lld rule");
		goto out;
	}

	lld_item_links_sort(&lld_rows);

	if (SUCCEED != lld_update_triggers(hostid, lld_ruleid, &lld_rows, &lld_macro_paths, error))
	{
		treegix_log(LOG_LEVEL_DEBUG, "cannot update/add triggers because parent host was removed while"
				" processing lld rule");
		goto out;
	}

	if (SUCCEED != lld_update_graphs(hostid, lld_ruleid, &lld_rows, &lld_macro_paths, error))
	{
		treegix_log(LOG_LEVEL_DEBUG, "cannot update/add graphs because parent host was removed while"
				" processing lld rule");
		goto out;
	}

	lld_update_hosts(lld_ruleid, &lld_rows, &lld_macro_paths, error, lifetime, now);

	/* add informative warning to the error message about lack of data for macros used in filter */
	if (NULL != info)
		*error = trx_strdcat(*error, info);
out:
	trx_free(info);
	trx_free(discovery_key);

	lld_filter_clean(&filter);

	trx_vector_ptr_clear_ext(&lld_rows, (trx_clean_func_t)lld_row_free);
	trx_vector_ptr_destroy(&lld_rows);
	trx_vector_ptr_clear_ext(&lld_macro_paths, (trx_clean_func_t)trx_lld_macro_path_free);
	trx_vector_ptr_destroy(&lld_macro_paths);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}
