

#include "common.h"
#include "daemon.h"
#include "trxself.h"
#include "log.h"
#include "trxipcservice.h"
#include "trxserialize.h"
#include "preprocessing.h"
#include "trxembed.h"

#include "sysinfo.h"
#include "preproc_worker.h"
#include "item_preproc.h"
#include "preproc_history.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

#define TRX_PREPROC_VALUE_PREVIEW_LEN		100

trx_es_t	es_engine;

/******************************************************************************
 *                                                                            *
 * Function: worker_format_value                                              *
 *                                                                            *
 * Purpose: formats value in text format                                      *
 *                                                                            *
 * Parameters: value     - [IN] the value to format                           *
 *             value_str - [OUT] the formatted value                          *
 *                                                                            *
 * Comments: Control characters are replaced with '.' and truncated if it's   *
 *           larger than TRX_PREPROC_VALUE_PREVIEW_LEN characters.            *
 *                                                                            *
 ******************************************************************************/
static void	worker_format_value(const trx_variant_t *value, char **value_str)
{
	int		len, i;
	const char	*value_desc;

	value_desc = trx_variant_value_desc(value);

	if (TRX_PREPROC_VALUE_PREVIEW_LEN < trx_strlen_utf8(value_desc))
	{
		/* truncate value and append '...' */
		len = trx_strlen_utf8_nchars(value_desc, TRX_PREPROC_VALUE_PREVIEW_LEN - TRX_CONST_STRLEN("..."));
		*value_str = trx_malloc(NULL, len + TRX_CONST_STRLEN("...") + 1);
		memcpy(*value_str, value_desc, len);
		memcpy(*value_str + len, "...", TRX_CONST_STRLEN("...") + 1);
	}
	else
	{
		*value_str = trx_malloc(NULL, (len = strlen(value_desc)) + 1);
		memcpy(*value_str, value_desc, len + 1);
	}

	/* replace control characters */
	for (i = 0; i < len; i++)
	{
		if (0 != iscntrl((*value_str)[i]))
			(*value_str)[i] = '.';
	}
}

/******************************************************************************
 *                                                                            *
 * Function: worker_format_result                                             *
 *                                                                            *
 * Purpose: formats one preprocessing step result                             *
 *                                                                            *
 * Parameters: step   - [IN] the preprocessing step number                    *
 *             result - [IN] the preprocessing step result                    *
 *             error  - [IN] the preprocessing step error (can be NULL)       *
 *             out    - [OUT] the formatted string                            *
 *                                                                            *
 ******************************************************************************/
static void	worker_format_result(int step, const trx_preproc_result_t *result, const char *error, char **out)
{
	char	*actions[] = {"", " (discard value)", " (set value)", " (set error)"};

	if (NULL == error)
	{
		char	*value_str;

		worker_format_value(&result->value, &value_str);
		*out = trx_dsprintf(NULL, "%d. Result%s: %s\n", step, actions[result->action], value_str);
		trx_free(value_str);
	}
	else
	{
		*out = trx_dsprintf(NULL, "%d. Failed%s: %s\n", step, actions[result->action], error);
		trx_rtrim(*out, TRX_WHITESPACE);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: worker_format_error                                              *
 *                                                                            *
 * Purpose: formats preprocessing error message                               *
 *                                                                            *
 * Parameters: value        - [IN] the input value                            *
 *             results      - [IN] the preprocessing step results             *
 *             results_num  - [IN] the number of executed steps               *
 *             errmsg       - [IN] the error message of last executed step    *
 *             error        - [OUT] the formatted error message               *
 *                                                                            *
 ******************************************************************************/
static void	worker_format_error(const trx_variant_t *value, trx_preproc_result_t *results, int results_num,
		const char *errmsg, char **error)
{
	char			*value_str, *err_step;
	int			i;
	size_t			error_alloc = 512, error_offset = 0;
	trx_vector_str_t	results_str;
	trx_db_mock_field_t	field;

	trx_vector_str_create(&results_str);

	/* add header to error message */
	*error = trx_malloc(NULL, error_alloc);
	worker_format_value(value, &value_str);
	trx_snprintf_alloc(error, &error_alloc, &error_offset, "Preprocessing failed for: %s\n", value_str);
	trx_free(value_str);

	trx_db_mock_field_init(&field, TRX_TYPE_CHAR, ITEM_ERROR_LEN);

	trx_db_mock_field_append(&field, *error);
	trx_db_mock_field_append(&field, "...\n");

	/* format the last (failed) step */
	worker_format_result(results_num, &results[results_num - 1], errmsg, &err_step);
	trx_vector_str_append(&results_str, err_step);

	if (SUCCEED == trx_db_mock_field_append(&field, err_step))
	{
		/* format the first steps */
		for (i = results_num - 2; i >= 0; i--)
		{
			worker_format_result(i + 1, &results[i], NULL, &err_step);

			if (SUCCEED != trx_db_mock_field_append(&field, err_step))
			{
				trx_free(err_step);
				break;
			}

			trx_vector_str_append(&results_str, err_step);
		}
	}

	/* add steps to error message */

	if (results_str.values_num < results_num)
		trx_strcpy_alloc(error, &error_alloc, &error_offset, "...\n");

	for (i = results_str.values_num - 1; i >= 0; i--)
		trx_strcpy_alloc(error, &error_alloc, &error_offset, results_str.values[i]);

	/* truncate formatted error if necessary */
	if (ITEM_ERROR_LEN < trx_strlen_utf8(*error))
	{
		char	*ptr;

		ptr = (*error) + trx_db_strlen_n(*error, ITEM_ERROR_LEN - 3);
		for (i = 0; i < 3; i++)
			*ptr++ = '.';
		*ptr = '\0';
	}

	trx_vector_str_clear_ext(&results_str, trx_str_free);
	trx_vector_str_destroy(&results_str);
}

/******************************************************************************
 *                                                                            *
 * Function: worker_item_preproc_execute                                      *
 *                                                                            *
 * Purpose: execute preprocessing steps                                       *
 *                                                                            *
 * Parameters: value_type    - [IN] the item value type                       *
 *             value         - [IN/OUT] the value to process                  *
 *             ts            - [IN] the value timestamp                       *
 *             steps         - [IN] the preprocessing steps to execute        *
 *             steps_num     - [IN] the number of preprocessing steps         *
 *             history_in    - [IN] the preprocessing history                 *
 *             history_out   - [OUT] the new preprocessing history            *
 *             results       - [OUT] the preprocessing step results           *
 *             results_num   - [OUT] the number of step results               *
 *             error         - [OUT] error message                            *
 *                                                                            *
 * Return value: SUCCEED - the preprocessing steps finished successfully      *
 *               FAIL - otherwise, error contains the error message           *
 *                                                                            *
 ******************************************************************************/
static int	worker_item_preproc_execute(unsigned char value_type, trx_variant_t *value, const trx_timespec_t *ts,
		trx_preproc_op_t *steps, int steps_num, trx_vector_ptr_t *history_in, trx_vector_ptr_t *history_out,
		trx_preproc_result_t *results, int *results_num, char **error)
{
	int		i, ret = SUCCEED;

	for (i = 0; i < steps_num; i++)
	{
		trx_preproc_op_t	*op = &steps[i];
		trx_variant_t		history_value;
		trx_timespec_t		history_ts;

		trx_preproc_history_pop_value(history_in, i, &history_value, &history_ts);

		if (FAIL == (ret = trx_item_preproc(value_type, value, ts, op, &history_value, &history_ts, error)))
		{
			results[i].action = op->error_handler;
			ret = trx_item_preproc_handle_error(value, op, error);
			trx_variant_clear(&history_value);
		}
		else
			results[i].action = TRX_PREPROC_FAIL_DEFAULT;

		if (SUCCEED == ret)
		{
			if (NULL == *error)
			{
				/* result history is kept to report results of steps before failing step, */
				/* which means it can be omitted for the last step.                       */
				if (i != steps_num - 1)
					trx_variant_copy(&results[i].value, value);
				else
					trx_variant_set_none(&results[i].value);
			}
			else
			{
				/* preprocessing step successfully extracted error, set it */
				results[i].action = TRX_PREPROC_FAIL_FORCE_ERROR;
				ret = FAIL;
			}
		}

		if (SUCCEED != ret)
		{
			break;
		}

		if (TRX_VARIANT_NONE != history_value.type)
		{
			/* the value is byte copied to history_out vector and doesn't have to be cleared */
			trx_preproc_history_add_value(history_out, i, &history_value, &history_ts);
		}

		if (TRX_VARIANT_NONE == value->type)
			break;
	}

	*results_num = (i == steps_num ? i : i + 1);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: worker_preprocess_value                                          *
 *                                                                            *
 * Purpose: handle item value preprocessing task                              *
 *                                                                            *
 * Parameters: socket  - [IN] IPC socket                                      *
 *             message - [IN] packed preprocessing task                       *
 *                                                                            *
 ******************************************************************************/
static void	worker_preprocess_value(trx_ipc_socket_t *socket, trx_ipc_message_t *message)
{
	trx_uint32_t		size = 0;
	unsigned char		*data = NULL, value_type;
	trx_uint64_t		itemid;
	trx_variant_t		value, value_start;
	int			i, steps_num, results_num, ret;
	char			*errmsg = NULL, *error = NULL;
	trx_timespec_t		*ts;
	trx_preproc_op_t	*steps;
	trx_vector_ptr_t	history_in, history_out;
	trx_preproc_result_t	*results;

	trx_vector_ptr_create(&history_in);
	trx_vector_ptr_create(&history_out);

	trx_preprocessor_unpack_task(&itemid, &value_type, &ts, &value, &history_in, &steps, &steps_num,
			message->data);

	trx_variant_copy(&value_start, &value);
	results = (trx_preproc_result_t *)trx_malloc(NULL, sizeof(trx_preproc_result_t) * steps_num);
	memset(results, 0, sizeof(trx_preproc_result_t) * steps_num);

	if (FAIL == (ret = worker_item_preproc_execute(value_type, &value, ts, steps, steps_num, &history_in,
			&history_out, results, &results_num, &errmsg)) && 0 != results_num)
	{
		int action = results[results_num - 1].action;

		if (TRX_PREPROC_FAIL_SET_ERROR != action && TRX_PREPROC_FAIL_FORCE_ERROR != action)
		{
			worker_format_error(&value_start, results, results_num, errmsg, &error);
			trx_free(errmsg);
		}
		else
			error = errmsg;
	}

	if (SUCCEED == TRX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
	{
		const char	*result;

		result = (SUCCEED == ret ? trx_variant_value_desc(&value) : error);
		treegix_log(LOG_LEVEL_DEBUG, "%s(): %s", __func__, trx_variant_value_desc(&value_start));
		treegix_log(LOG_LEVEL_DEBUG, "%s: %s %s",__func__,  trx_result_string(ret), result);
	}

	size = trx_preprocessor_pack_result(&data, &value, &history_out, error);
	trx_variant_clear(&value);
	trx_free(error);
	trx_free(ts);
	trx_free(steps);

	if (FAIL == trx_ipc_socket_write(socket, TRX_IPC_PREPROCESSOR_RESULT, data, size))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot send preprocessing result");
		exit(EXIT_FAILURE);
	}

	trx_free(data);

	trx_variant_clear(&value_start);

	for (i = 0; i < results_num; i++)
		trx_variant_clear(&results[i].value);
	trx_free(results);

	trx_vector_ptr_clear_ext(&history_out, (trx_clean_func_t)trx_preproc_op_history_free);
	trx_vector_ptr_destroy(&history_out);

	trx_vector_ptr_clear_ext(&history_in, (trx_clean_func_t)trx_preproc_op_history_free);
	trx_vector_ptr_destroy(&history_in);
}

/******************************************************************************
 *                                                                            *
 * Function: worker_test_value                                                *
 *                                                                            *
 * Purpose: handle item value test preprocessing task                         *
 *                                                                            *
 * Parameters: socket  - [IN] IPC socket                                      *
 *             message - [IN] packed preprocessing task                       *
 *                                                                            *
 ******************************************************************************/
static void	worker_test_value(trx_ipc_socket_t *socket, trx_ipc_message_t *message)
{
	trx_uint32_t		size;
	unsigned char		*data, value_type;
	trx_variant_t		value, value_start;
	int			i, steps_num, results_num;
	char			*error = NULL, *value_str;
	trx_timespec_t		ts;
	trx_preproc_op_t	*steps;
	trx_vector_ptr_t	history_in, history_out;
	trx_preproc_result_t	*results;

	trx_vector_ptr_create(&history_in);
	trx_vector_ptr_create(&history_out);

	trx_preprocessor_unpack_test_request(&value_type, &value_str, &ts, &history_in, &steps, &steps_num,
			message->data);

	trx_variant_set_str(&value, value_str);
	trx_variant_copy(&value_start, &value);

	results = (trx_preproc_result_t *)trx_malloc(NULL, sizeof(trx_preproc_result_t) * steps_num);
	memset(results, 0, sizeof(trx_preproc_result_t) * steps_num);

	trx_item_preproc_test(value_type, &value, &ts, steps, steps_num, &history_in, &history_out, results,
			&results_num, &error);

	size = trx_preprocessor_pack_test_result(&data, results, results_num, &history_out, error);

	if (FAIL == trx_ipc_socket_write(socket, TRX_IPC_PREPROCESSOR_TEST_RESULT, data, size))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot send preprocessing result");
		exit(EXIT_FAILURE);
	}

	trx_variant_clear(&value);
	trx_free(error);
	trx_free(steps);
	trx_free(data);

	trx_variant_clear(&value_start);

	for (i = 0; i < results_num; i++)
	{
		trx_variant_clear(&results[i].value);
		trx_free(results[i].error);
	}
	trx_free(results);

	trx_vector_ptr_clear_ext(&history_out, (trx_clean_func_t)trx_preproc_op_history_free);
	trx_vector_ptr_destroy(&history_out);

	trx_vector_ptr_clear_ext(&history_in, (trx_clean_func_t)trx_preproc_op_history_free);
	trx_vector_ptr_destroy(&history_in);
}

TRX_THREAD_ENTRY(preprocessing_worker_thread, args)
{
	pid_t			ppid;
	char			*error = NULL;
	trx_ipc_socket_t	socket;
	trx_ipc_message_t	message;

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	trx_setproctitle("%s #%d starting", get_process_type_string(process_type), process_num);

	trx_es_init(&es_engine);

	trx_ipc_message_init(&message);

	if (FAIL == trx_ipc_socket_open(&socket, TRX_IPC_SERVICE_PREPROCESSING, SEC_PER_MIN, &error))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot connect to preprocessing service: %s", error);
		trx_free(error);
		exit(EXIT_FAILURE);
	}

	ppid = getppid();
	trx_ipc_socket_write(&socket, TRX_IPC_PREPROCESSOR_WORKER, (unsigned char *)&ppid, sizeof(ppid));

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	trx_setproctitle("%s #%d started", get_process_type_string(process_type), process_num);

	update_selfmon_counter(TRX_PROCESS_STATE_BUSY);

	while (TRX_IS_RUNNING())
	{
		update_selfmon_counter(TRX_PROCESS_STATE_IDLE);

		if (SUCCEED != trx_ipc_socket_read(&socket, &message))
		{
			treegix_log(LOG_LEVEL_CRIT, "cannot read preprocessing service request");
			exit(EXIT_FAILURE);
		}

		update_selfmon_counter(TRX_PROCESS_STATE_BUSY);
		trx_update_env(trx_time());

		switch (message.code)
		{
			case TRX_IPC_PREPROCESSOR_REQUEST:
				worker_preprocess_value(&socket, &message);
				break;
			case TRX_IPC_PREPROCESSOR_TEST_REQUEST:
				worker_test_value(&socket, &message);
				break;
		}

		trx_ipc_message_clean(&message);
	}

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);

	trx_es_destroy(&es_engine);
}
