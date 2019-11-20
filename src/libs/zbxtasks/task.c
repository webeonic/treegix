

#include <assert.h>

#include "common.h"
#include "log.h"

#include "db.h"
#include "trxjson.h"
#include "trxtasks.h"

/******************************************************************************
 *                                                                            *
 * Function: tm_remote_command_clear                                          *
 *                                                                            *
 * Purpose: frees remote command task resources                               *
 *                                                                            *
 * Parameters: data - [IN] the remote command task data                       *
 *                                                                            *
 ******************************************************************************/
static void	tm_remote_command_clear(trx_tm_remote_command_t *data)
{
	trx_free(data->command);
	trx_free(data->username);
	trx_free(data->password);
	trx_free(data->publickey);
	trx_free(data->privatekey);
}

/******************************************************************************
 *                                                                            *
 * Function: tm_remote_command_result_clear                                   *
 *                                                                            *
 * Purpose: frees remote command result task resources                        *
 *                                                                            *
 * Parameters: data - [IN] the remote command result task data                *
 *                                                                            *
 ******************************************************************************/
static void	tm_remote_command_result_clear(trx_tm_remote_command_result_t *data)
{
	trx_free(data->info);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tm_task_clear                                                *
 *                                                                            *
 * Purpose: frees task resources                                              *
 *                                                                            *
 * Parameters: task - [IN]                                                    *
 *                                                                            *
 ******************************************************************************/
void	trx_tm_task_clear(trx_tm_task_t *task)
{
	if (NULL != task->data)
	{
		switch (task->type)
		{
			case TRX_TM_TASK_REMOTE_COMMAND:
				tm_remote_command_clear((trx_tm_remote_command_t *)task->data);
				break;
			case TRX_TM_TASK_REMOTE_COMMAND_RESULT:
				tm_remote_command_result_clear((trx_tm_remote_command_result_t *)task->data);
				break;
			case TRX_TM_TASK_CHECK_NOW:
				/* nothing to clear */
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
		}
	}

	trx_free(task->data);
	task->type = TRX_TM_TASK_UNDEFINED;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tm_task_free                                                 *
 *                                                                            *
 * Purpose: frees task and its resources                                      *
 *                                                                            *
 * Parameters: task - [IN] the task to free                                   *
 *                                                                            *
 ******************************************************************************/
void	trx_tm_task_free(trx_tm_task_t *task)
{
	trx_tm_task_clear(task);
	trx_free(task);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tm_remote_command_create                                     *
 *                                                                            *
 * Purpose: create a remote command task data                                 *
 *                                                                            *
 * Parameters: command_type  - [IN] the remote command type (TRX_SCRIPT_TYPE_)*
 *             command       - [IN] the command to execute                    *
 *             execute_on    - [IN] the execution target (TRX_SCRIPT_EXECUTE_)*
 *             port          - [IN] the target port                           *
 *             authtype      - [IN] the authentication type                   *
 *             username      - [IN] the username (can be NULL)                *
 *             password      - [IN] the password (can be NULL)                *
 *             publickey     - [IN] the public key (can be NULL)              *
 *             privatekey    - [IN] the private key (can be NULL)             *
 *             parent_taskid - [IN] the parent task identifier                *
 *             hostid        - [IN] the target host identifier                *
 *             alertid       - [IN] the alert identifier                      *
 *                                                                            *
 * Return value: The created remote command data.                             *
 *                                                                            *
 ******************************************************************************/
trx_tm_remote_command_t	*trx_tm_remote_command_create(int command_type, const char *command, int execute_on, int port,
		int authtype, const char *username, const char *password, const char *publickey, const char *privatekey,
		trx_uint64_t parent_taskid, trx_uint64_t hostid, trx_uint64_t alertid)
{
	trx_tm_remote_command_t	*data;

	data = (trx_tm_remote_command_t *)trx_malloc(NULL, sizeof(trx_tm_remote_command_t));
	data->command_type = command_type;
	data->command = trx_strdup(NULL, TRX_NULL2EMPTY_STR(command));
	data->execute_on = execute_on;
	data->port = port;
	data->authtype = authtype;
	data->username = trx_strdup(NULL, TRX_NULL2EMPTY_STR(username));
	data->password = trx_strdup(NULL, TRX_NULL2EMPTY_STR(password));
	data->publickey = trx_strdup(NULL, TRX_NULL2EMPTY_STR(publickey));
	data->privatekey = trx_strdup(NULL, TRX_NULL2EMPTY_STR(privatekey));
	data->parent_taskid = parent_taskid;
	data->hostid = hostid;
	data->alertid = alertid;

	return data;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tm_remote_command_result_create                              *
 *                                                                            *
 * Purpose: create a remote command result task data                          *
 *                                                                            *
 * Parameters: parent_taskid - [IN] the parent task identifier                *
 *             status        - [IN] the remote command execution status       *
 *             info          - [IN] the remote command execution result       *
 *                                                                            *
 * Return value: The created remote command result data.                      *
 *                                                                            *
 ******************************************************************************/
trx_tm_remote_command_result_t	*trx_tm_remote_command_result_create(trx_uint64_t parent_taskid, int status,
		const char *info)
{
	trx_tm_remote_command_result_t	*data;

	data = (trx_tm_remote_command_result_t *)trx_malloc(NULL, sizeof(trx_tm_remote_command_result_t));
	data->status = status;
	data->parent_taskid = parent_taskid;
	data->info = trx_strdup(NULL, TRX_NULL2EMPTY_STR(info));

	return data;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tm_check_now_create                                          *
 *                                                                            *
 * Purpose: create a check now task data                                      *
 *                                                                            *
 * Parameters: itemid - [IN] the item identifier                              *
 *                                                                            *
 * Return value: The created check now data.                                  *
 *                                                                            *
 ******************************************************************************/
trx_tm_check_now_t	*trx_tm_check_now_create(trx_uint64_t itemid)
{
	trx_tm_check_now_t	*data;

	data = (trx_tm_check_now_t *)trx_malloc(NULL, sizeof(trx_tm_check_now_t));
	data->itemid = itemid;

	return data;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tm_task_create                                               *
 *                                                                            *
 * Purpose: create a new task                                                 *
 *                                                                            *
 * Parameters: taskid       - [IN] the task identifier                        *
 *             type         - [IN] the task type (see TRX_TM_TASK_*)          *
 *             status       - [IN] the task status (see TRX_TM_STATUS_*)      *
 *             clock        - [IN] the task creation time                     *
 *             ttl          - [IN] the task expiration period in seconds      *
 *             proxy_hostid - [IN] the destination proxy identifier (or 0)    *
 *                                                                            *
 * Return value: The created task.                                            *
 *                                                                            *
 ******************************************************************************/
trx_tm_task_t	*trx_tm_task_create(trx_uint64_t taskid, unsigned char type, unsigned char status, int clock, int ttl,
		trx_uint64_t proxy_hostid)
{
	trx_tm_task_t	*task;

	task = (trx_tm_task_t *)trx_malloc(NULL, sizeof(trx_tm_task_t));

	task->taskid = taskid;
	task->type = type;
	task->status = status;
	task->clock = clock;
	task->ttl = ttl;
	task->proxy_hostid = proxy_hostid;
	task->data = NULL;

	return task;
}

/******************************************************************************
 *                                                                            *
 * Function: tm_save_remote_command_tasks                                     *
 *                                                                            *
 * Purpose: saves remote command task data in database                        *
 *                                                                            *
 * Parameters: tasks     - [IN] the tasks                                     *
 *             tasks_num - [IN] the number of tasks to process                *
 *                                                                            *
 * Return value: SUCCEED - the data was saved successfully                    *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: The tasks array can contain mixture of task types.               *
 *                                                                            *
 ******************************************************************************/
static int	tm_save_remote_command_tasks(trx_tm_task_t **tasks, int tasks_num)
{
	int			i, ret;
	trx_db_insert_t		db_insert;
	trx_tm_remote_command_t	*data;

	trx_db_insert_prepare(&db_insert, "task_remote_command", "taskid", "command_type", "execute_on", "port",
			"authtype", "username", "password", "publickey", "privatekey", "command", "alertid",
			"parent_taskid", "hostid", NULL);

	for (i = 0; i < tasks_num; i++)
	{
		trx_tm_task_t	*task = tasks[i];

		switch (task->type)
		{
			case TRX_TM_TASK_REMOTE_COMMAND:
				data = (trx_tm_remote_command_t *)task->data;
				trx_db_insert_add_values(&db_insert, task->taskid, data->command_type, data->execute_on,
						data->port, data->authtype, data->username, data->password,
						data->publickey, data->privatekey, data->command, data->alertid,
						data->parent_taskid, data->hostid);
		}
	}

	ret = trx_db_insert_execute(&db_insert);
	trx_db_insert_clean(&db_insert);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: tm_save_remote_command_result_tasks                              *
 *                                                                            *
 * Purpose: saves remote command result task data in database                 *
 *                                                                            *
 * Parameters: tasks     - [IN] the tasks                                     *
 *             tasks_num - [IN] the number of tasks to process                *
 *                                                                            *
 * Return value: SUCCEED - the data was saved successfully                    *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: The tasks array can contain mixture of task types.               *
 *                                                                            *
 ******************************************************************************/
static int	tm_save_remote_command_result_tasks(trx_tm_task_t **tasks, int tasks_num)
{
	int				i, ret;
	trx_db_insert_t			db_insert;
	trx_tm_remote_command_result_t	*data;

	trx_db_insert_prepare(&db_insert, "task_remote_command_result", "taskid", "status", "parent_taskid", "info",
			NULL);

	for (i = 0; i < tasks_num; i++)
	{
		trx_tm_task_t	*task = tasks[i];

		switch (task->type)
		{
			case TRX_TM_TASK_REMOTE_COMMAND_RESULT:
				data = (trx_tm_remote_command_result_t *)task->data;
				trx_db_insert_add_values(&db_insert, task->taskid, data->status, data->parent_taskid,
						data->info);
		}
	}

	ret = trx_db_insert_execute(&db_insert);
	trx_db_insert_clean(&db_insert);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: tm_save_check_now_tasks                                          *
 *                                                                            *
 * Purpose: saves remote command task data in database                        *
 *                                                                            *
 * Parameters: tasks     - [IN] the tasks                                     *
 *             tasks_num - [IN] the number of tasks to process                *
 *                                                                            *
 * Return value: SUCCEED - the data was saved successfully                    *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: The tasks array can contain mixture of task types.               *
 *                                                                            *
 ******************************************************************************/
static int	tm_save_check_now_tasks(trx_tm_task_t **tasks, int tasks_num)
{
	int			i, ret;
	trx_db_insert_t		db_insert;
	trx_tm_check_now_t	*data;

	trx_db_insert_prepare(&db_insert, "task_check_now", "taskid", "itemid", NULL);

	for (i = 0; i < tasks_num; i++)
	{
		const trx_tm_task_t	*task = tasks[i];

		switch (task->type)
		{
			case TRX_TM_TASK_CHECK_NOW:
				data = (trx_tm_check_now_t *)task->data;
				trx_db_insert_add_values(&db_insert, task->taskid, data->itemid);
		}
	}

	ret = trx_db_insert_execute(&db_insert);
	trx_db_insert_clean(&db_insert);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: tm_save_tasks                                                    *
 *                                                                            *
 * Purpose: saves tasks into database                                         *
 *                                                                            *
 * Parameters: tasks     - [IN] the tasks                                     *
 *             tasks_num - [IN] the number of tasks to process                *
 *                                                                            *
 * Return value: SUCCEED - the tasks were saved successfully                  *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	tm_save_tasks(trx_tm_task_t **tasks, int tasks_num)
{
	int		i, ret, remote_command_num = 0, remote_command_result_num = 0, check_now_num = 0, ids_num = 0;
	trx_uint64_t	taskid;
	trx_db_insert_t	db_insert;

	for (i = 0; i < tasks_num; i++)
	{
		if (0 == tasks[i]->taskid)
			ids_num++;
	}

	if (0 != ids_num)
		taskid = DBget_maxid_num("task", ids_num);

	for (i = 0; i < tasks_num; i++)
	{
		switch (tasks[i]->type)
		{
			case TRX_TM_TASK_REMOTE_COMMAND:
				remote_command_num++;
				break;
			case TRX_TM_TASK_REMOTE_COMMAND_RESULT:
				remote_command_result_num++;
				break;
			case TRX_TM_TASK_CHECK_NOW:
				check_now_num++;
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
		}

		if (0 == tasks[i]->taskid)
			tasks[i]->taskid = taskid++;
	}

	trx_db_insert_prepare(&db_insert, "task", "taskid", "type", "status", "clock", "ttl", "proxy_hostid", NULL);

	for (i = 0; i < tasks_num; i++)
	{
		if (0 == tasks[i]->taskid)
			continue;

		trx_db_insert_add_values(&db_insert, tasks[i]->taskid, (int)tasks[i]->type, (int)tasks[i]->status,
				tasks[i]->clock, tasks[i]->ttl, tasks[i]->proxy_hostid);
	}

	ret = trx_db_insert_execute(&db_insert);
	trx_db_insert_clean(&db_insert);

	if (SUCCEED == ret && 0 != remote_command_num)
		ret = tm_save_remote_command_tasks(tasks, tasks_num);

	if (SUCCEED == ret && 0 != remote_command_result_num)
		ret = tm_save_remote_command_result_tasks(tasks, tasks_num);

	if (SUCCEED == ret && 0 != check_now_num)
		ret = tm_save_check_now_tasks(tasks, tasks_num);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tm_save_tasks                                                *
 *                                                                            *
 * Purpose: saves tasks and their data into database                          *
 *                                                                            *
 * Parameters: tasks - [IN] the tasks                                         *
 *                                                                            *
 ******************************************************************************/
void	trx_tm_save_tasks(trx_vector_ptr_t *tasks)
{
	treegix_log(LOG_LEVEL_DEBUG, "In %s() tasks_num:%d", __func__, tasks->values_num);

	tm_save_tasks((trx_tm_task_t **)tasks->values, tasks->values_num);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tm_save_task                                                 *
 *                                                                            *
 * Purpose: saves task and its data into database                             *
 *                                                                            *
 * Parameters: task - [IN] the task                                           *
 *                                                                            *
 * Return value: SUCCEED - the task was saved successfully                    *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
int	trx_tm_save_task(trx_tm_task_t *task)
{
	int	ret;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	ret = tm_save_tasks(&task, 1);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tm_update_task_status                                        *
 *                                                                            *
 * Purpose: update status of the specified tasks in database                  *
 *                                                                            *
 * Parameters: tasks  - [IN] the tasks                                        *
 *             status - [IN] the new status                                   *
 *                                                                            *
 ******************************************************************************/
void	trx_tm_update_task_status(trx_vector_ptr_t *tasks, int status)
{
	trx_vector_uint64_t	taskids;
	int			i;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&taskids);

	for (i = 0; i < tasks->values_num; i++)
	{
		trx_tm_task_t	*task = (trx_tm_task_t *)tasks->values[i];
		trx_vector_uint64_append(&taskids, task->taskid);
	}

	trx_vector_uint64_sort(&taskids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update task set status=%d where", status);
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "taskid", taskids.values, taskids.values_num);
	DBexecute("%s", sql);
	trx_free(sql);

	trx_vector_uint64_destroy(&taskids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: tm_json_serialize_task                                           *
 *                                                                            *
 * Purpose: serializes common task data in json format                        *
 *                                                                            *
 * Parameters: json - [OUT] the json data                                     *
 *             data - [IN] the task to serialize                              *
 *                                                                            *
 ******************************************************************************/
static void	tm_json_serialize_task(struct trx_json *json, const trx_tm_task_t *task)
{
	trx_json_addint64(json, TRX_PROTO_TAG_TYPE, task->type);
	trx_json_addint64(json, TRX_PROTO_TAG_CLOCK, task->clock);
	trx_json_addint64(json, TRX_PROTO_TAG_TTL, task->ttl);
}

/******************************************************************************
 *                                                                            *
 * Function: tm_json_serialize_remote_command                                 *
 *                                                                            *
 * Purpose: serializes remote command data in json format                     *
 *                                                                            *
 * Parameters: json - [OUT] the json data                                     *
 *             data - [IN] the remote command to serialize                    *
 *                                                                            *
 ******************************************************************************/
static void	tm_json_serialize_remote_command(struct trx_json *json, const trx_tm_remote_command_t *data)
{
	trx_json_addint64(json, TRX_PROTO_TAG_COMMANDTYPE, data->command_type);
	trx_json_addstring(json, TRX_PROTO_TAG_COMMAND, data->command, TRX_JSON_TYPE_STRING);
	trx_json_addint64(json, TRX_PROTO_TAG_EXECUTE_ON, data->execute_on);
	trx_json_addint64(json, TRX_PROTO_TAG_PORT, data->port);
	trx_json_addint64(json, TRX_PROTO_TAG_AUTHTYPE, data->authtype);
	trx_json_addstring(json, TRX_PROTO_TAG_USERNAME, data->username, TRX_JSON_TYPE_STRING);
	trx_json_addstring(json, TRX_PROTO_TAG_PASSWORD, data->password, TRX_JSON_TYPE_STRING);
	trx_json_addstring(json, TRX_PROTO_TAG_PUBLICKEY, data->publickey, TRX_JSON_TYPE_STRING);
	trx_json_addstring(json, TRX_PROTO_TAG_PRIVATEKEY, data->privatekey, TRX_JSON_TYPE_STRING);
	trx_json_adduint64(json, TRX_PROTO_TAG_ALERTID, data->alertid);
	trx_json_adduint64(json, TRX_PROTO_TAG_PARENT_TASKID, data->parent_taskid);
	trx_json_adduint64(json, TRX_PROTO_TAG_HOSTID, data->hostid);
}

/******************************************************************************
 *                                                                            *
 * Function: tm_json_serialize_remote_command_result                          *
 *                                                                            *
 * Purpose: serializes remote command result data in json format              *
 *                                                                            *
 * Parameters: json - [OUT] the json data                                     *
 *             data - [IN] the remote command result to serialize             *
 *                                                                            *
 ******************************************************************************/
static void	tm_json_serialize_remote_command_result(struct trx_json *json,
		const trx_tm_remote_command_result_t *data)
{
	trx_json_addint64(json, TRX_PROTO_TAG_STATUS, data->status);
	trx_json_addstring(json, TRX_PROTO_TAG_INFO, data->info, TRX_JSON_TYPE_STRING);
	trx_json_adduint64(json, TRX_PROTO_TAG_PARENT_TASKID, data->parent_taskid);
}

/******************************************************************************
 *                                                                            *
 * Function: tm_json_serialize_check_now                                      *
 *                                                                            *
 * Purpose: serializes check now data in json format                          *
 *                                                                            *
 * Parameters: json - [OUT] the json data                                     *
 *             data - [IN] the check now to serialize                         *
 *                                                                            *
 ******************************************************************************/
static void	tm_json_serialize_check_now(struct trx_json *json, const trx_tm_check_now_t *data)
{
	trx_json_addint64(json, TRX_PROTO_TAG_ITEMID, data->itemid);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tm_json_serialize_tasks                                      *
 *                                                                            *
 * Purpose: serializes remote command data in json format                     *
 *                                                                            *
 * Parameters: json  - [OUT] the json data                                    *
 *             tasks - [IN] the tasks to serialize                            *
 *                                                                            *
 ******************************************************************************/
void	trx_tm_json_serialize_tasks(struct trx_json *json, const trx_vector_ptr_t *tasks)
{
	int	i;

	trx_json_addarray(json, TRX_PROTO_TAG_TASKS);

	for (i = 0; i < tasks->values_num; i++)
	{
		const trx_tm_task_t	*task = (const trx_tm_task_t *)tasks->values[i];

		trx_json_addobject(json, NULL);
		tm_json_serialize_task(json, task);

		switch (task->type)
		{
			case TRX_TM_TASK_REMOTE_COMMAND:
				tm_json_serialize_remote_command(json, (trx_tm_remote_command_t *)task->data);
				break;
			case TRX_TM_TASK_REMOTE_COMMAND_RESULT:
				tm_json_serialize_remote_command_result(json, (trx_tm_remote_command_result_t *)task->data);
				break;
			case TRX_TM_TASK_CHECK_NOW:
				tm_json_serialize_check_now(json, (trx_tm_check_now_t *)task->data);
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
				break;
		}

		trx_json_close(json);
	}

	trx_json_close(json);
}

/******************************************************************************
 *                                                                            *
 * Function: tm_json_deserialize_remote_command                               *
 *                                                                            *
 * Purpose: deserializes remote command from json data                        *
 *                                                                            *
 * Parameters: jp - [IN] the json data                                        *
 *                                                                            *
 * Return value: The deserialized remote command data or NULL if              *
 *               deserialization failed.                                      *
 *                                                                            *
 ******************************************************************************/
static trx_tm_remote_command_t	*tm_json_deserialize_remote_command(const struct trx_json_parse *jp)
{
	char			value[MAX_STRING_LEN];
	int			commandtype, execute_on, port, authtype;
	trx_uint64_t		alertid, parent_taskid, hostid;
	char			*username = NULL, *password = NULL, *publickey = NULL, *privatekey = NULL,
				*command = NULL;
	size_t			username_alloc = 0, password_alloc = 0, publickey_alloc = 0, privatekey_alloc = 0,
				command_alloc = 0;
	trx_tm_remote_command_t	*data = NULL;

	if (SUCCEED != trx_json_value_by_name(jp, TRX_PROTO_TAG_COMMANDTYPE, value, sizeof(value)))
		goto out;

	commandtype = atoi(value);

	if (SUCCEED != trx_json_value_by_name(jp, TRX_PROTO_TAG_EXECUTE_ON, value, sizeof(value)))
		goto out;

	execute_on = atoi(value);

	if (SUCCEED != trx_json_value_by_name(jp, TRX_PROTO_TAG_PORT, value, sizeof(value)))
		goto out;

	port = atoi(value);

	if (SUCCEED != trx_json_value_by_name(jp, TRX_PROTO_TAG_AUTHTYPE, value, sizeof(value)))
		goto out;

	authtype = atoi(value);

	if (SUCCEED != trx_json_value_by_name(jp, TRX_PROTO_TAG_ALERTID, value, sizeof(value)) ||
			SUCCEED != is_uint64(value, &alertid))
	{
		goto out;
	}

	if (SUCCEED != trx_json_value_by_name(jp, TRX_PROTO_TAG_PARENT_TASKID, value, sizeof(value)) ||
			SUCCEED != is_uint64(value, &parent_taskid))
	{
		goto out;
	}

	if (SUCCEED != trx_json_value_by_name(jp, TRX_PROTO_TAG_HOSTID, value, sizeof(value)) ||
			SUCCEED != is_uint64(value, &hostid))
	{
		goto out;
	}

	if (SUCCEED != trx_json_value_by_name_dyn(jp, TRX_PROTO_TAG_USERNAME, &username, &username_alloc))
		goto out;

	if (SUCCEED != trx_json_value_by_name_dyn(jp, TRX_PROTO_TAG_PASSWORD, &password, &password_alloc))
		goto out;

	if (SUCCEED != trx_json_value_by_name_dyn(jp, TRX_PROTO_TAG_PUBLICKEY, &publickey, &publickey_alloc))
		goto out;

	if (SUCCEED != trx_json_value_by_name_dyn(jp, TRX_PROTO_TAG_PRIVATEKEY, &privatekey, &privatekey_alloc))
		goto out;

	if (SUCCEED != trx_json_value_by_name_dyn(jp, TRX_PROTO_TAG_COMMAND, &command, &command_alloc))
		goto out;

	data = trx_tm_remote_command_create(commandtype, command, execute_on, port, authtype, username, password,
			publickey, privatekey, parent_taskid, hostid, alertid);
out:
	trx_free(command);
	trx_free(privatekey);
	trx_free(publickey);
	trx_free(password);
	trx_free(username);

	return data;
}

/******************************************************************************
 *                                                                            *
 * Function: tm_json_deserialize_remote_command_result                        *
 *                                                                            *
 * Purpose: deserializes remote command result from json data                 *
 *                                                                            *
 * Parameters: jp - [IN] the json data                                        *
 *                                                                            *
 * Return value: The deserialized remote command result data or NULL if       *
 *               deserialization failed.                                      *
 *                                                                            *
 ******************************************************************************/
static trx_tm_remote_command_result_t	*tm_json_deserialize_remote_command_result(const struct trx_json_parse *jp)
{
	char				value[MAX_STRING_LEN];
	int				status;
	trx_uint64_t			parent_taskid;
	char				*info = NULL;
	size_t				info_alloc = 0;
	trx_tm_remote_command_result_t	*data = NULL;

	if (SUCCEED != trx_json_value_by_name(jp, TRX_PROTO_TAG_STATUS, value, sizeof(value)))
		goto out;

	status = atoi(value);

	if (SUCCEED != trx_json_value_by_name(jp, TRX_PROTO_TAG_PARENT_TASKID, value, sizeof(value)) ||
			SUCCEED != is_uint64(value, &parent_taskid))
	{
		goto out;
	}

	if (SUCCEED != trx_json_value_by_name_dyn(jp, TRX_PROTO_TAG_INFO, &info, &info_alloc))
		goto out;

	data = trx_tm_remote_command_result_create(parent_taskid, status, info);
out:
	trx_free(info);

	return data;
}

/******************************************************************************
 *                                                                            *
 * Function: tm_json_deserialize_check_now                                    *
 *                                                                            *
 * Purpose: deserializes check now from json data                             *
 *                                                                            *
 * Parameters: jp - [IN] the json data                                        *
 *                                                                            *
 * Return value: The deserialized check now data or NULL if deserialization   *
 *               failed.                                                      *
 *                                                                            *
 ******************************************************************************/
static trx_tm_check_now_t	*tm_json_deserialize_check_now(const struct trx_json_parse *jp)
{
	char		value[MAX_ID_LEN + 1];
	trx_uint64_t	itemid;

	if (SUCCEED != trx_json_value_by_name(jp, TRX_PROTO_TAG_ITEMID, value, sizeof(value)) ||
			SUCCEED != is_uint64(value, &itemid))
	{
		return NULL;
	}

	return trx_tm_check_now_create(itemid);
}

/******************************************************************************
 *                                                                            *
 * Function: tm_json_deserialize_task                                         *
 *                                                                            *
 * Purpose: deserializes common task data from json data                      *
 *                                                                            *
 * Parameters: jp - [IN] the json data                                        *
 *                                                                            *
 * Return value: The deserialized task data or NULL if deserialization failed.*
 *                                                                            *
 ******************************************************************************/
static trx_tm_task_t	*tm_json_deserialize_task(const struct trx_json_parse *jp)
{
	char	value[MAX_STRING_LEN];
	int	type, clock, ttl;

	if (SUCCEED != trx_json_value_by_name(jp, TRX_PROTO_TAG_TYPE, value, sizeof(value)))
		return NULL;

	TRX_STR2UCHAR(type, value);

	if (SUCCEED != trx_json_value_by_name(jp, TRX_PROTO_TAG_CLOCK, value, sizeof(value)))
		return NULL;

	clock = atoi(value);

	if (SUCCEED != trx_json_value_by_name(jp, TRX_PROTO_TAG_TTL, value, sizeof(value)))
		return NULL;

	ttl = atoi(value);

	return trx_tm_task_create(0, type, TRX_TM_STATUS_NEW, clock, ttl, 0);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tm_json_deserialize_tasks                                    *
 *                                                                            *
 * Purpose: deserializes tasks from json data                                 *
 *                                                                            *
 * Parameters: jp    - [IN] the json data                                     *
 *             tasks - [OUT] the deserialized tasks                           *
 *                                                                            *
 ******************************************************************************/
void	trx_tm_json_deserialize_tasks(const struct trx_json_parse *jp, trx_vector_ptr_t *tasks)
{
	const char		*pnext = NULL;
	struct trx_json_parse	jp_task;

	while (NULL != (pnext = trx_json_next(jp, pnext)))
	{
		trx_tm_task_t	*task;

		if (SUCCEED != trx_json_brackets_open(pnext, &jp_task))
		{
			treegix_log(LOG_LEVEL_DEBUG, "Cannot deserialize task record: %s", jp->start);
			continue;
		}

		task = tm_json_deserialize_task(&jp_task);

		if (NULL == task)
		{
			treegix_log(LOG_LEVEL_DEBUG, "Cannot deserialize task at: %s", jp_task.start);
			continue;
		}

		switch (task->type)
		{
			case TRX_TM_TASK_REMOTE_COMMAND:
				task->data = tm_json_deserialize_remote_command(&jp_task);
				break;
			case TRX_TM_TASK_REMOTE_COMMAND_RESULT:
				task->data = tm_json_deserialize_remote_command_result(&jp_task);
				break;
			case TRX_TM_TASK_CHECK_NOW:
				task->data = tm_json_deserialize_check_now(&jp_task);
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
				break;
		}

		if (NULL == task->data)
		{
			treegix_log(LOG_LEVEL_DEBUG, "Cannot deserialize task data at: %s", jp_task.start);
			trx_tm_task_free(task);
			continue;
		}

		trx_vector_ptr_append(tasks, task);
	}
}
