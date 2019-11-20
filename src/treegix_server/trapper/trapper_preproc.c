

#include "common.h"
#include "log.h"
#include "trxjson.h"
#include "trxalgo.h"
#include "preproc.h"
#include "trapper_preproc.h"
#include "../preprocessor/preproc_history.h"

/******************************************************************************
 *                                                                            *
 * Function: trapper_parse_preproc_test                                       *
 *                                                                            *
 * Purpose: parses preprocessing test request                                 *
 *                                                                            *
 * Parameters: jp         - [IN] the request                                  *
 *             values     - [OUT] the values to test optional                 *
 *                                (history + current)                         *
 *             ts         - [OUT] value timestamps                            *
 *             values_num - [OUT] the number of values                        *
 *             value_type - [OUT] the value type                              *
 *             steps      - [OUT] the preprocessing steps                     *
 *             error      - [OUT] the error message                           *
 *                                                                            *
 * Return value: SUCCEED - the request was parsed successfully                *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	trapper_parse_preproc_test(const struct trx_json_parse *jp, char **values, trx_timespec_t *ts,
		int *values_num, unsigned char *value_type, trx_vector_ptr_t *steps, int *single, char **error)
{
	char			buffer[MAX_STRING_LEN], *step_params = NULL, *error_handler_params = NULL;
	const char		*ptr;
	trx_user_t		user;
	int			ret = FAIL;
	struct trx_json_parse	jp_data, jp_history, jp_steps, jp_step;
	size_t			size;
	trx_timespec_t		ts_now;

	if (FAIL == trx_json_value_by_name(jp, TRX_PROTO_TAG_SID, buffer, sizeof(buffer)) ||
			SUCCEED != DBget_user_by_active_session(buffer, &user) || USER_TYPE_TREEGIX_ADMIN > user.type)
	{
		*error = trx_strdup(NULL, "Permission denied.");
		goto out;
	}

	if (FAIL == trx_json_brackets_by_name(jp, TRX_PROTO_TAG_DATA, &jp_data))
	{
		*error = trx_strdup(NULL, "Missing data field.");
		goto out;
	}

	if (FAIL == trx_json_value_by_name(&jp_data, TRX_PROTO_TAG_VALUE_TYPE, buffer, sizeof(buffer)))
	{
		*error = trx_strdup(NULL, "Missing value type field.");
		goto out;
	}
	*value_type = atoi(buffer);

	if (FAIL == trx_json_value_by_name(&jp_data, TRX_PROTO_TAG_SINGLE, buffer, sizeof(buffer)))
		*single = 0;
	else
		*single = (0 == strcmp(buffer, "true") ? 1 : 0);

	trx_timespec(&ts_now);
	if (SUCCEED == trx_json_brackets_by_name(&jp_data, TRX_PROTO_TAG_HISTORY, &jp_history))
	{
		size = 0;
		if (FAIL == trx_json_value_by_name_dyn(&jp_history, TRX_PROTO_TAG_VALUE, values, &size))
		{
			*error = trx_strdup(NULL, "Missing history value field.");
			goto out;
		}
		(*values_num)++;

		if (FAIL == trx_json_value_by_name(&jp_history, TRX_PROTO_TAG_TIMESTAMP, buffer, sizeof(buffer)))
		{
			*error = trx_strdup(NULL, "Missing history timestamp field.");
			goto out;
		}

		if (0 != strncmp(buffer, "now", TRX_CONST_STRLEN("now")))
		{
			*error = trx_dsprintf(NULL, "invalid history value timestamp: %s", buffer);
			goto out;
		}

		ts[0] = ts_now;
		ptr = buffer + TRX_CONST_STRLEN("now");

		if ('\0' != *ptr)
		{
			int	delay;

			if ('-' != *ptr || FAIL == is_time_suffix(ptr + 1, &delay, strlen(ptr + 1)))
			{
				*error = trx_dsprintf(NULL, "invalid history value timestamp: %s", buffer);
				goto out;
			}

			ts[0].sec -= delay;
		}
	}

	size = 0;
	if (FAIL == trx_json_value_by_name_dyn(&jp_data, TRX_PROTO_TAG_VALUE, &values[*values_num], &size))
	{
		*error = trx_strdup(NULL, "Missing value field.");
		goto out;
	}
	ts[(*values_num)++] = ts_now;

	if (FAIL == trx_json_brackets_by_name(&jp_data, TRX_PROTO_TAG_STEPS, &jp_steps))
	{
		*error = trx_strdup(NULL, "Missing preprocessing steps field.");
		goto out;
	}

	for (ptr = NULL; NULL != (ptr = trx_json_next(&jp_steps, ptr));)
	{
		trx_preproc_op_t	*step;
		unsigned char		step_type, error_handler;

		if (FAIL == trx_json_brackets_open(ptr, &jp_step))
		{
			*error = trx_strdup(NULL, "Cannot parse preprocessing step.");
			goto out;
		}

		if (FAIL == trx_json_value_by_name(&jp_step, TRX_PROTO_TAG_TYPE, buffer, sizeof(buffer)))
		{
			*error = trx_strdup(NULL, "Missing preprocessing step type field.");
			goto out;
		}
		step_type = atoi(buffer);

		if (FAIL == trx_json_value_by_name(&jp_step, TRX_PROTO_TAG_ERROR_HANDLER, buffer, sizeof(buffer)))
		{
			*error = trx_strdup(NULL, "Missing preprocessing step type error handler field.");
			goto out;
		}
		error_handler = atoi(buffer);

		size = 0;
		if (FAIL == trx_json_value_by_name_dyn(&jp_step, TRX_PROTO_TAG_PARAMS, &step_params, &size))
		{
			*error = trx_strdup(NULL, "Missing preprocessing step type params field.");
			goto out;
		}

		size = 0;
		if (FAIL == trx_json_value_by_name_dyn(&jp_step, TRX_PROTO_TAG_ERROR_HANDLER_PARAMS,
				&error_handler_params, &size))
		{
			*error = trx_strdup(NULL, "Missing preprocessing step type error handler params field.");
			goto out;
		}

		step = (trx_preproc_op_t *)trx_malloc(NULL, sizeof(trx_preproc_op_t));
		step->type = step_type;
		step->params = step_params;
		step->error_handler = error_handler;
		step->error_handler_params = error_handler_params;
		trx_vector_ptr_append(steps, step);

		step_params = NULL;
		error_handler_params = NULL;
	}

	ret = SUCCEED;
out:
	if (FAIL == ret)
	{
		trx_vector_ptr_clear_ext(steps, (trx_clean_func_t)trx_preproc_op_free);
		trx_free(values[0]);
		trx_free(values[1]);
	}

	trx_free(step_params);
	trx_free(error_handler_params);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trapper_preproc_test_run                                         *
 *                                                                            *
 * Purpose: executes preprocessing test request                               *
 *                                                                            *
 * Parameters: jp    - [IN] the request                                       *
 *             json  - [OUT] the output json                                  *
 *             error - [OUT] the error message                                *
 *                                                                            *
 * Return value: SUCCEED - the request was executed successfully              *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: This function will fail if the request format is not valid or    *
 *           there was connection (to preprocessing manager) error.           *
 *           Any errors in the preprocessing itself are reported in output    *
 *           json and success is returned.                                    *
 *                                                                            *
 ******************************************************************************/
static int	trapper_preproc_test_run(const struct trx_json_parse *jp, struct trx_json *json, char **error)
{
	char			*values[2] = {NULL, NULL}, *preproc_error = NULL;
	int			ret = FAIL, i, values_num = 0, single;
	unsigned char		value_type;
	trx_vector_ptr_t	steps, results, history;
	trx_timespec_t		ts[2];
	trx_preproc_result_t	*result;

	trx_vector_ptr_create(&steps);
	trx_vector_ptr_create(&results);
	trx_vector_ptr_create(&history);

	if (FAIL == trapper_parse_preproc_test(jp, values, ts, &values_num, &value_type, &steps, &single, error))
		goto out;

	for (i = 0; i < values_num; i++)
	{
		trx_vector_ptr_clear_ext(&results, (trx_clean_func_t)trx_preproc_result_free);
		if (FAIL == trx_preprocessor_test(value_type, values[i], &ts[i], &steps, &results, &history,
				&preproc_error, error))
		{
			goto out;
		}

		if (NULL != preproc_error)
			break;

		if (0 == single)
		{
			result = (trx_preproc_result_t *)results.values[results.values_num - 1];
			if (TRX_VARIANT_NONE != result->value.type &&
					FAIL == trx_variant_to_value_type(&result->value, value_type, &preproc_error))
			{
				break;
			}
		}
	}

	trx_json_addstring(json, TRX_PROTO_TAG_RESPONSE, "success", TRX_JSON_TYPE_STRING);
	trx_json_addobject(json, TRX_PROTO_TAG_DATA);

	if (i + 1 < values_num)
		trx_json_addstring(json, TRX_PROTO_TAG_PREVIOUS, "true", TRX_JSON_TYPE_INT);

	trx_json_addarray(json, TRX_PROTO_TAG_STEPS);
	for (i = 0; i < results.values_num; i++)
	{
		result = (trx_preproc_result_t *)results.values[i];

		trx_json_addobject(json, NULL);

		if (NULL != result->error)
			trx_json_addstring(json, TRX_PROTO_TAG_ERROR, result->error, TRX_JSON_TYPE_STRING);

		if (TRX_PREPROC_FAIL_DEFAULT != result->action)
			trx_json_adduint64(json, TRX_PROTO_TAG_ACTION, result->action);

		if (i == results.values_num - 1 && NULL != result->error)
		{
			if (TRX_PREPROC_FAIL_SET_ERROR == result->action)
				trx_json_addstring(json, TRX_PROTO_TAG_FAILED, preproc_error, TRX_JSON_TYPE_STRING);
		}

		if (TRX_VARIANT_NONE != result->value.type)
		{
			trx_json_addstring(json, TRX_PROTO_TAG_RESULT, trx_variant_value_desc(&result->value),
					TRX_JSON_TYPE_STRING);
		}
		else if (NULL == result->error || TRX_PREPROC_FAIL_DISCARD_VALUE == result->action)
			trx_json_addstring(json, TRX_PROTO_TAG_RESULT, NULL, TRX_JSON_TYPE_NULL);

		trx_json_close(json);
	}
	trx_json_close(json);

	if (NULL == preproc_error)
	{
		result = (trx_preproc_result_t *)results.values[results.values_num - 1];

		if (TRX_VARIANT_NONE != result->value.type)
		{
			trx_json_addstring(json, TRX_PROTO_TAG_RESULT, trx_variant_value_desc(&result->value),
					TRX_JSON_TYPE_STRING);
		}
		else
			trx_json_addstring(json, TRX_PROTO_TAG_RESULT, NULL, TRX_JSON_TYPE_NULL);
	}
	else
		trx_json_addstring(json, TRX_PROTO_TAG_ERROR, preproc_error, TRX_JSON_TYPE_STRING);

	ret = SUCCEED;
out:
	for (i = 0; i < values_num; i++)
		trx_free(values[i]);

	trx_free(preproc_error);

	trx_vector_ptr_clear_ext(&history, (trx_clean_func_t)trx_preproc_op_history_free);
	trx_vector_ptr_destroy(&history);
	trx_vector_ptr_clear_ext(&results, (trx_clean_func_t)trx_preproc_result_free);
	trx_vector_ptr_destroy(&results);
	trx_vector_ptr_clear_ext(&steps, (trx_clean_func_t)trx_preproc_op_free);
	trx_vector_ptr_destroy(&steps);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_trapper_preproc_test                                         *
 *                                                                            *
 * Purpose: processes preprocessing test request                              *
 *                                                                            *
 * Parameters: sock - [IN] the request source socket (frontend)               *
 *             jp   - [IN] the request                                        *
 *                                                                            *
 * Return value: SUCCEED - the request was processed successfully             *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: This function will send proper (success/fail) response to the    *
 *           request socket.                                                  *
 *           Preprocessing failure (error returned by a preprocessing step)   *
 *           is counted as successful test and will return success response.  *
 *                                                                            *
 ******************************************************************************/
int	trx_trapper_preproc_test(trx_socket_t *sock, const struct trx_json_parse *jp)
{
	char		*error = NULL;
	int		ret;
	struct trx_json	json;

	trx_json_init(&json, 1024);

	if (SUCCEED == (ret = trapper_preproc_test_run(jp, &json, &error)))
	{
		trx_tcp_send_bytes_to(sock, json.buffer, json.buffer_size, CONFIG_TIMEOUT);
	}
	else
	{
		trx_send_response(sock, ret, error, CONFIG_TIMEOUT);
		trx_free(error);
	}

	trx_json_free(&json);

	return ret;
}

#ifdef HAVE_TESTS
#	include "../../../tests/treegix_server/trapper/trapper_preproc_test_run.c"
#endif
