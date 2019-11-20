

#include "checks_calculated.h"
#include "trxserver.h"
#include "log.h"
#include "../../libs/trxserver/evalfunc.h"

typedef struct
{
	int	functionid;
	char	*host;
	char	*key;
	char	*func;
	char	*params;
	char	*value;
}
function_t;

typedef struct
{
	char		*exp;
	function_t	*functions;
	int		functions_alloc;
	int		functions_num;
}
expression_t;

static void	free_expression(expression_t *exp)
{
	function_t	*f;
	int		i;

	for (i = 0; i < exp->functions_num; i++)
	{
		f = &exp->functions[i];
		trx_free(f->host);
		trx_free(f->key);
		trx_free(f->func);
		trx_free(f->params);
		trx_free(f->value);
	}

	trx_free(exp->exp);
	trx_free(exp->functions);
	exp->functions_alloc = 0;
	exp->functions_num = 0;
}

static int	calcitem_add_function(expression_t *exp, char *host, char *key, char *func, char *params)
{
	function_t	*f;

	if (exp->functions_alloc == exp->functions_num)
	{
		exp->functions_alloc += 8;
		exp->functions = (function_t *)trx_realloc(exp->functions, exp->functions_alloc * sizeof(function_t));
	}

	f = &exp->functions[exp->functions_num++];
	f->functionid = exp->functions_num;
	f->host = host;
	f->key = key;
	f->func = func;
	f->params = params;
	f->value = NULL;

	return f->functionid;
}

static int	calcitem_parse_expression(DC_ITEM *dc_item, expression_t *exp, char *error, int max_error_len)
{
	char	*e, *buf = NULL;
	size_t	exp_alloc = 128, exp_offset = 0, f_pos, par_l, par_r;
	int	ret = NOTSUPPORTED;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() expression:'%s'", __func__, dc_item->params);

	exp->exp = (char *)trx_malloc(exp->exp, exp_alloc);

	for (e = dc_item->params; SUCCEED == trx_function_find(e, &f_pos, &par_l, &par_r, error, max_error_len);
			e += par_r + 1)
	{
		char	*func, *params, *host = NULL, *key = NULL;
		size_t	param_pos, param_len, sep_pos;
		int	functionid, quoted;

		/* copy the part of the string preceding function */
		trx_strncpy_alloc(&exp->exp, &exp_alloc, &exp_offset, e, f_pos);

		/* extract the first function parameter and <host:>key reference from it */

		trx_function_param_parse(e + par_l + 1, &param_pos, &param_len, &sep_pos);

		trx_free(buf);
		buf = trx_function_param_unquote_dyn(e + par_l + 1 + param_pos, param_len, &quoted);

		if (SUCCEED != parse_host_key(buf, &host, &key))
		{
			trx_snprintf(error, max_error_len, "Invalid first parameter in function [%.*s].",
					(int)(par_r - f_pos + 1), e + f_pos);
			goto out;
		}
		if (NULL == host)
			host = trx_strdup(NULL, dc_item->host.host);

		/* extract function name and remaining parameters */

		e[par_l] = '\0';
		func = trx_strdup(NULL, e + f_pos);
		e[par_l] = '(';

		if (')' != e[par_l + 1 + sep_pos]) /* first parameter is not the only one */
		{
			e[par_r] = '\0';
			params = trx_strdup(NULL, e + par_l + 1 + sep_pos + 1);
			e[par_r] = ')';
		}
		else	/* the only parameter of the function was <host:>key reference */
			params = trx_strdup(NULL, "");

		functionid = calcitem_add_function(exp, host, key, func, params);

		treegix_log(LOG_LEVEL_DEBUG, "%s() functionid:%d function:'%s:%s.%s(%s)'",
				__func__, functionid, host, key, func, params);

		/* substitute function with id in curly brackets */
		trx_snprintf_alloc(&exp->exp, &exp_alloc, &exp_offset, "{%d}", functionid);
	}

	if (par_l > par_r)
		goto out;

	/* copy the remaining part */
	trx_strcpy_alloc(&exp->exp, &exp_alloc, &exp_offset, e);

	treegix_log(LOG_LEVEL_DEBUG, "%s() expression:'%s'", __func__, exp->exp);

	if (SUCCEED == substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, &dc_item->host, NULL, NULL, NULL,
			&exp->exp, MACRO_TYPE_ITEM_EXPRESSION, error, max_error_len))
	{
		ret = SUCCEED;
	}
out:
	trx_free(buf);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

static int	calcitem_evaluate_expression(expression_t *exp, char *error, size_t max_error_len,
		trx_vector_ptr_t *unknown_msgs)
{
	function_t	*f = NULL;
	char		*buf, replace[16], *errstr = NULL;
	int		i, ret = SUCCEED;
	trx_host_key_t	*keys = NULL;
	DC_ITEM		*items = NULL;
	int		*errcodes = NULL;
	trx_timespec_t	ts;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (0 == exp->functions_num)
		return ret;

	keys = (trx_host_key_t *)trx_malloc(keys, sizeof(trx_host_key_t) * (size_t)exp->functions_num);
	items = (DC_ITEM *)trx_malloc(items, sizeof(DC_ITEM) * (size_t)exp->functions_num);
	errcodes = (int *)trx_malloc(errcodes, sizeof(int) * (size_t)exp->functions_num);

	for (i = 0; i < exp->functions_num; i++)
	{
		keys[i].host = exp->functions[i].host;
		keys[i].key = exp->functions[i].key;
	}

	DCconfig_get_items_by_keys(items, keys, errcodes, exp->functions_num);

	trx_timespec(&ts);

	for (i = 0; i < exp->functions_num; i++)
	{
		int	ret_unknown = 0;	/* flag raised if current function evaluates to TRX_UNKNOWN */
		char	*unknown_msg;

		f = &exp->functions[i];

		if (SUCCEED != errcodes[i])
		{
			trx_snprintf(error, max_error_len,
					"Cannot evaluate function \"%s(%s)\":"
					" item \"%s:%s\" does not exist.",
					f->func, f->params, f->host, f->key);
			ret = NOTSUPPORTED;
			break;
		}

		/* do not evaluate if the item is disabled or belongs to a disabled host */

		if (ITEM_STATUS_ACTIVE != items[i].status)
		{
			trx_snprintf(error, max_error_len,
					"Cannot evaluate function \"%s(%s)\":"
					" item \"%s:%s\" is disabled.",
					f->func, f->params, f->host, f->key);
			ret = NOTSUPPORTED;
			break;
		}

		if (HOST_STATUS_MONITORED != items[i].host.status)
		{
			trx_snprintf(error, max_error_len,
					"Cannot evaluate function \"%s(%s)\":"
					" item \"%s:%s\" belongs to a disabled host.",
					f->func, f->params, f->host, f->key);
			ret = NOTSUPPORTED;
			break;
		}

		/* If the item is NOTSUPPORTED then evaluation is allowed for:   */
		/*   - functions white-listed in evaluatable_for_notsupported(). */
		/*     Their values can be evaluated to regular numbers even for */
		/*     NOTSUPPORTED items. */
		/*   - other functions. Result of evaluation is TRX_UNKNOWN.     */

		if (ITEM_STATE_NOTSUPPORTED == items[i].state && FAIL == evaluatable_for_notsupported(f->func))
		{
			/* compose and store 'unknown' message for future use */
			unknown_msg = trx_dsprintf(NULL,
					"Cannot evaluate function \"%s(%s)\": item \"%s:%s\" not supported.",
					f->func, f->params, f->host, f->key);

			trx_vector_ptr_append(unknown_msgs, unknown_msg);
			ret_unknown = 1;
		}

		f->value = (char *)trx_malloc(f->value, MAX_BUFFER_LEN);

		if (0 == ret_unknown &&
				SUCCEED != evaluate_function(f->value, &items[i], f->func, f->params, &ts, &errstr))
		{
			/* compose and store error message for future use */
			if (NULL != errstr)
			{
				unknown_msg = trx_dsprintf(NULL, "Cannot evaluate function \"%s(%s)\": %s.",
						f->func, f->params, errstr);
				trx_free(errstr);
			}
			else
			{
				unknown_msg = trx_dsprintf(NULL, "Cannot evaluate function \"%s(%s)\".",
						f->func, f->params);
			}

			trx_vector_ptr_append(unknown_msgs, unknown_msg);
			ret_unknown = 1;
		}

		if (1 == ret_unknown || SUCCEED != is_double_suffix(f->value, TRX_FLAG_DOUBLE_SUFFIX) || '-' == *f->value)
		{
			char	*wrapped;

			if (0 == ret_unknown)
			{
				wrapped = trx_dsprintf(NULL, "(%s)", f->value);
			}
			else
			{
				/* write a special token of unknown value with 'unknown' message number, like */
				/* TRX_UNKNOWN0, TRX_UNKNOWN1 etc. not wrapped in () */
				wrapped = trx_dsprintf(NULL, TRX_UNKNOWN_STR "%d", unknown_msgs->values_num - 1);
			}

			trx_free(f->value);
			f->value = wrapped;
		}
		else
			f->value = (char *)trx_realloc(f->value, strlen(f->value) + 1);

		trx_snprintf(replace, sizeof(replace), "{%d}", f->functionid);
		buf = string_replace(exp->exp, replace, f->value);
		trx_free(exp->exp);
		exp->exp = buf;
	}

	DCconfig_clean_items(items, errcodes, exp->functions_num);

	trx_free(errcodes);
	trx_free(items);
	trx_free(keys);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

int	get_value_calculated(DC_ITEM *dc_item, AGENT_RESULT *result)
{
	expression_t		exp;
	int			ret;
	char			error[MAX_STRING_LEN];
	double			value;
	trx_vector_ptr_t	unknown_msgs;		/* pointers to messages about origins of 'unknown' values */

	treegix_log(LOG_LEVEL_DEBUG, "In %s() key:'%s' expression:'%s'", __func__, dc_item->key_orig, dc_item->params);

	memset(&exp, 0, sizeof(exp));

	if (SUCCEED != (ret = calcitem_parse_expression(dc_item, &exp, error, sizeof(error))))
	{
		SET_MSG_RESULT(result, strdup(error));
		goto clean1;
	}

	/* Assumption: most often there will be no NOTSUPPORTED items and function errors. */
	/* Therefore initialize error messages vector but do not reserve any space. */
	trx_vector_ptr_create(&unknown_msgs);

	if (SUCCEED != (ret = calcitem_evaluate_expression(&exp, error, sizeof(error), &unknown_msgs)))
	{
		SET_MSG_RESULT(result, strdup(error));
		goto clean;
	}

	if (SUCCEED != evaluate(&value, exp.exp, error, sizeof(error), &unknown_msgs))
	{
		SET_MSG_RESULT(result, strdup(error));
		ret = NOTSUPPORTED;
		goto clean;
	}

	treegix_log(LOG_LEVEL_DEBUG, "%s() value:" TRX_FS_DBL, __func__, value);

	if (ITEM_VALUE_TYPE_UINT64 == dc_item->value_type && 0 > value)
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Received value [" TRX_FS_DBL "]"
				" is not suitable for value type [%s].",
				value, trx_item_value_type_string((trx_item_value_type_t)dc_item->value_type)));
		ret = NOTSUPPORTED;
		goto clean;
	}

	SET_DBL_RESULT(result, value);
clean:
	trx_vector_ptr_clear_ext(&unknown_msgs, trx_ptr_free);
	trx_vector_ptr_destroy(&unknown_msgs);
clean1:
	free_expression(&exp);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}
