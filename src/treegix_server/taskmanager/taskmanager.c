

#include "common.h"
#include "daemon.h"
#include "trxself.h"
#include "log.h"
#include "db.h"
#include "dbcache.h"
#include "trxtasks.h"
#include "../events.h"
#include "../actions.h"
#include "export.h"
#include "taskmanager.h"

#define TRX_TM_PROCESS_PERIOD		5
#define TRX_TM_CLEANUP_PERIOD		SEC_PER_HOUR
#define TRX_TASKMANAGER_TIMEOUT		5

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

/******************************************************************************
 *                                                                            *
 * Function: tm_execute_task_close_problem                                    *
 *                                                                            *
 * Purpose: close the specified problem event and remove task                 *
 *                                                                            *
 * Parameters: triggerid         - [IN] the source trigger id                 *
 *             eventid           - [IN] the problem eventid to close          *
 *             userid            - [IN] the user that requested to close the  *
 *                                      problem                               *
 *                                                                            *
 ******************************************************************************/
static void	tm_execute_task_close_problem(trx_uint64_t taskid, trx_uint64_t triggerid, trx_uint64_t eventid,
		trx_uint64_t userid)
{
	DB_RESULT	result;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() eventid:" TRX_FS_UI64, __func__, eventid);

	result = DBselect("select null from problem where eventid=" TRX_FS_UI64 " and r_eventid is null", eventid);

	/* check if the task hasn't been already closed by another process */
	if (NULL != DBfetch(result))
		trx_close_problem(triggerid, eventid, userid);

	DBfree_result(result);

	DBexecute("update task set status=%d where taskid=" TRX_FS_UI64, TRX_TM_STATUS_DONE, taskid);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: tm_try_task_close_problem                                        *
 *                                                                            *
 * Purpose: try to close problem by event acknowledgement action              *
 *                                                                            *
 * Parameters: taskid - [IN] the task identifier                              *
 *                                                                            *
 * Return value: SUCCEED - task was executed and removed                      *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	tm_try_task_close_problem(trx_uint64_t taskid)
{
	DB_ROW			row;
	DB_RESULT		result;
	int			ret = FAIL;
	trx_uint64_t		userid, triggerid, eventid;
	trx_vector_uint64_t	triggerids, locked_triggerids;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() taskid:" TRX_FS_UI64, __func__, taskid);

	trx_vector_uint64_create(&triggerids);
	trx_vector_uint64_create(&locked_triggerids);

	result = DBselect("select a.userid,a.eventid,e.objectid"
				" from task_close_problem tcp,acknowledges a"
				" left join events e"
					" on a.eventid=e.eventid"
				" where tcp.taskid=" TRX_FS_UI64
					" and tcp.acknowledgeid=a.acknowledgeid",
			taskid);

	if (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(triggerid, row[2]);
		trx_vector_uint64_append(&triggerids, triggerid);
		DCconfig_lock_triggers_by_triggerids(&triggerids, &locked_triggerids);

		/* only close the problem if source trigger was successfully locked */
		if (0 != locked_triggerids.values_num)
		{
			TRX_STR2UINT64(userid, row[0]);
			TRX_STR2UINT64(eventid, row[1]);
			tm_execute_task_close_problem(taskid, triggerid, eventid, userid);

			DCconfig_unlock_triggers(&locked_triggerids);

			ret = SUCCEED;
		}
	}
	DBfree_result(result);

	trx_vector_uint64_destroy(&locked_triggerids);
	trx_vector_uint64_destroy(&triggerids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: tm_expire_remote_command                                         *
 *                                                                            *
 * Purpose: process expired remote command task                               *
 *                                                                            *
 ******************************************************************************/
static void	tm_expire_remote_command(trx_uint64_t taskid)
{
	DB_ROW		row;
	DB_RESULT	result;
	trx_uint64_t	alertid;
	char		*error;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() taskid:" TRX_FS_UI64, __func__, taskid);

	DBbegin();

	result = DBselect("select alertid from task_remote_command where taskid=" TRX_FS_UI64, taskid);

	if (NULL != (row = DBfetch(result)))
	{
		if (SUCCEED != DBis_null(row[0]))
		{
			TRX_STR2UINT64(alertid, row[0]);

			error = DBdyn_escape_string_len("Remote command has been expired.", ALERT_ERROR_LEN);
			DBexecute("update alerts set error='%s',status=%d where alertid=" TRX_FS_UI64,
					error, ALERT_STATUS_FAILED, alertid);
			trx_free(error);
		}
	}

	DBfree_result(result);

	DBexecute("update task set status=%d where taskid=" TRX_FS_UI64, TRX_TM_STATUS_EXPIRED, taskid);

	DBcommit();

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: tm_process_remote_command_result                                 *
 *                                                                            *
 * Purpose: process remote command result task                                *
 *                                                                            *
 * Return value: SUCCEED - the task was processed successfully                *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	tm_process_remote_command_result(trx_uint64_t taskid)
{
	DB_ROW		row;
	DB_RESULT	result;
	trx_uint64_t	alertid, parent_taskid = 0;
	int		status, ret = FAIL;
	char		*error, *sql = NULL;
	size_t		sql_alloc = 0, sql_offset = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() taskid:" TRX_FS_UI64, __func__, taskid);

	DBbegin();

	result = DBselect("select r.status,r.info,a.alertid,r.parent_taskid"
			" from task_remote_command_result r"
			" left join task_remote_command c"
				" on c.taskid=r.parent_taskid"
			" left join alerts a"
				" on a.alertid=c.alertid"
			" where r.taskid=" TRX_FS_UI64, taskid);

	if (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(parent_taskid, row[3]);

		if (SUCCEED != DBis_null(row[2]))
		{
			TRX_STR2UINT64(alertid, row[2]);
			status = atoi(row[0]);

			if (SUCCEED == status)
			{
				DBexecute("update alerts set status=%d where alertid=" TRX_FS_UI64, ALERT_STATUS_SENT,
						alertid);
			}
			else
			{
				error = DBdyn_escape_string_len(row[1], ALERT_ERROR_LEN);
				DBexecute("update alerts set error='%s',status=%d where alertid=" TRX_FS_UI64,
						error, ALERT_STATUS_FAILED, alertid);
				trx_free(error);
			}
		}

		ret = SUCCEED;
	}

	DBfree_result(result);

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update task set status=%d where taskid=" TRX_FS_UI64,
			TRX_TM_STATUS_DONE, taskid);
	if (0 != parent_taskid)
		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " or taskid=" TRX_FS_UI64, parent_taskid);

	DBexecute("%s", sql);
	trx_free(sql);

	DBcommit();

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: tm_process_acknowledgements                                      *
 *                                                                            *
 * Purpose: process acknowledgements for alerts sending                       *
 *                                                                            *
 * Return value: The number of successfully processed tasks                   *
 *                                                                            *
 ******************************************************************************/
static int	tm_process_acknowledgements(trx_vector_uint64_t *ack_taskids)
{
	DB_ROW			row;
	DB_RESULT		result;
	int			processed_num = 0;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	trx_vector_ptr_t	ack_tasks;
	trx_ack_task_t		*ack_task;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() tasks_num:%d", __func__, ack_taskids->values_num);

	trx_vector_uint64_sort(ack_taskids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	trx_vector_ptr_create(&ack_tasks);

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select a.eventid,ta.acknowledgeid,ta.taskid"
			" from task_acknowledge ta"
			" left join acknowledges a"
				" on ta.acknowledgeid=a.acknowledgeid"
			" left join events e"
				" on a.eventid=e.eventid"
			" left join task t"
				" on ta.taskid=t.taskid"
			" where t.status=%d and",
			TRX_TM_STATUS_NEW);
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "t.taskid", ack_taskids->values, ack_taskids->values_num);
	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		if (SUCCEED == DBis_null(row[0]))
		{
			treegix_log(LOG_LEVEL_DEBUG, "cannot process acknowledge tasks because related event"
					" was removed");
			continue;
		}

		ack_task = (trx_ack_task_t *)trx_malloc(NULL, sizeof(trx_ack_task_t));

		TRX_STR2UINT64(ack_task->eventid, row[0]);
		TRX_STR2UINT64(ack_task->acknowledgeid, row[1]);
		TRX_STR2UINT64(ack_task->taskid, row[2]);
		trx_vector_ptr_append(&ack_tasks, ack_task);
	}
	DBfree_result(result);

	if (0 < ack_tasks.values_num)
	{
		trx_vector_ptr_sort(&ack_tasks, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
		processed_num = process_actions_by_acknowledgements(&ack_tasks);
	}

	sql_offset = 0;
	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset , "update task set status=%d where", TRX_TM_STATUS_DONE);
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "taskid", ack_taskids->values, ack_taskids->values_num);
	DBexecute("%s", sql);

	trx_free(sql);

	trx_vector_ptr_clear_ext(&ack_tasks, trx_ptr_free);
	trx_vector_ptr_destroy(&ack_tasks);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() processed:%d", __func__, processed_num);

	return processed_num;
}

/******************************************************************************
 *                                                                            *
 * Function: tm_process_check_now                                             *
 *                                                                            *
 * Purpose: process check now tasks for item rescheduling                     *
 *                                                                            *
 * Return value: The number of successfully processed tasks                   *
 *                                                                            *
 ******************************************************************************/
static int	tm_process_check_now(trx_vector_uint64_t *taskids)
{
	DB_ROW			row;
	DB_RESULT		result;
	int			i, processed_num = 0;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	trx_vector_ptr_t	tasks;
	trx_vector_uint64_t	done_taskids, itemids;
	trx_uint64_t		taskid, itemid, proxy_hostid, *proxy_hostids;
	trx_tm_task_t		*task;
	trx_tm_check_now_t	*data;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() tasks_num:%d", __func__, taskids->values_num);

	trx_vector_ptr_create(&tasks);
	trx_vector_uint64_create(&done_taskids);

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select t.taskid,t.status,t.proxy_hostid,td.itemid"
			" from task t"
			" left join task_check_now td"
				" on t.taskid=td.taskid"
			" where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "t.taskid", taskids->values, taskids->values_num);
	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(taskid, row[0]);

		if (SUCCEED == DBis_null(row[3]))
		{
			trx_vector_uint64_append(&done_taskids, taskid);
			continue;
		}

		TRX_DBROW2UINT64(proxy_hostid, row[2]);
		if (0 != proxy_hostid)
		{
			if (TRX_TM_STATUS_INPROGRESS == atoi(row[1]))
			{
				/* task has been sent to proxy, mark as done */
				trx_vector_uint64_append(&done_taskids, taskid);
				continue;
			}
		}

		TRX_STR2UINT64(itemid, row[3]);

		/* trx_task_t here is used only to store taskid, proxyhostid, data->itemid - */
		/* the rest of task properties are not used                                  */
		task = trx_tm_task_create(taskid, TRX_TM_TASK_CHECK_NOW, 0, 0, 0, proxy_hostid);
		task->data = (void *)trx_tm_check_now_create(itemid);
		trx_vector_ptr_append(&tasks, task);
	}
	DBfree_result(result);

	if (0 != tasks.values_num)
	{
		trx_vector_uint64_create(&itemids);

		for (i = 0; i < tasks.values_num; i++)
		{
			task = (trx_tm_task_t *)tasks.values[i];
			data = (trx_tm_check_now_t *)task->data;
			trx_vector_uint64_append(&itemids, data->itemid);
		}

		proxy_hostids = (trx_uint64_t *)trx_malloc(NULL, tasks.values_num * sizeof(trx_uint64_t));
		trx_dc_reschedule_items(&itemids, time(NULL), proxy_hostids);

		sql_offset = 0;
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

		for (i = 0; i < tasks.values_num; i++)
		{
			task = (trx_tm_task_t *)tasks.values[i];

			if (0 != proxy_hostids[i] && task->proxy_hostid == proxy_hostids[i])
				continue;

			trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset , "update task set");

			if (0 == proxy_hostids[i])
			{
				/* close tasks managed by server -                  */
				/* items either have been rescheduled or not cached */
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " status=%d", TRX_TM_STATUS_DONE);
				if (0 != task->proxy_hostid)
					trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ",proxy_hostid=null");

				processed_num++;
			}
			else
			{
				/* update target proxy hostid */
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " proxy_hostid=" TRX_FS_UI64,
						proxy_hostids[i]);
			}

			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where taskid=" TRX_FS_UI64 ";\n",
					task->taskid);

			DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
		}

		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (16 < sql_offset)	/* in ORACLE always present begin..end; */
			DBexecute("%s", sql);

		trx_vector_uint64_destroy(&itemids);
		trx_free(proxy_hostids);

		trx_vector_ptr_clear_ext(&tasks, (trx_clean_func_t)trx_tm_task_free);
	}

	if (0 != done_taskids.values_num)
	{
		sql_offset = 0;
		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update task set status=%d where",
				TRX_TM_STATUS_DONE);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "taskid", done_taskids.values,
				done_taskids.values_num);
		DBexecute("%s", sql);
	}

	trx_free(sql);
	trx_vector_uint64_destroy(&done_taskids);
	trx_vector_ptr_destroy(&tasks);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() processed:%d", __func__, processed_num);

	return processed_num;
}

/******************************************************************************
 *                                                                            *
 * Function: tm_expire_generic_tasks                                          *
 *                                                                            *
 * Purpose: expires tasks that don't require specific expiration handling     *
 *                                                                            *
 * Return value: The number of successfully expired tasks                     *
 *                                                                            *
 ******************************************************************************/
static int	tm_expire_generic_tasks(trx_vector_uint64_t *taskids)
{
	char		*sql = NULL;
	size_t		sql_alloc = 0, sql_offset = 0;

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update task set status=%d where", TRX_TM_STATUS_EXPIRED);
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "taskid", taskids->values, taskids->values_num);
	DBexecute("%s", sql);

	return taskids->values_num;
}

/******************************************************************************
 *                                                                            *
 * Function: tm_process_tasks                                                 *
 *                                                                            *
 * Purpose: process task manager tasks depending on task type                 *
 *                                                                            *
 * Return value: The number of successfully processed tasks                   *
 *                                                                            *
 ******************************************************************************/
static int	tm_process_tasks(int now)
{
	DB_ROW			row;
	DB_RESULT		result;
	int			type, processed_num = 0, expired_num = 0, clock, ttl;
	trx_uint64_t		taskid;
	trx_vector_uint64_t	ack_taskids, check_now_taskids, expire_taskids;

	trx_vector_uint64_create(&ack_taskids);
	trx_vector_uint64_create(&check_now_taskids);
	trx_vector_uint64_create(&expire_taskids);

	result = DBselect("select taskid,type,clock,ttl"
				" from task"
				" where status in (%d,%d)"
				" order by taskid",
			TRX_TM_STATUS_NEW, TRX_TM_STATUS_INPROGRESS);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(taskid, row[0]);
		TRX_STR2UCHAR(type, row[1]);
		clock = atoi(row[2]);
		ttl = atoi(row[3]);

		switch (type)
		{
			case TRX_TM_TASK_CLOSE_PROBLEM:
				/* close problem tasks will never have 'in progress' status */
				if (SUCCEED == tm_try_task_close_problem(taskid))
					processed_num++;
				break;
			case TRX_TM_TASK_REMOTE_COMMAND:
				/* both - 'new' and 'in progress' remote tasks should expire */
				if (0 != ttl && clock + ttl < now)
				{
					tm_expire_remote_command(taskid);
					expired_num++;
				}
				break;
			case TRX_TM_TASK_REMOTE_COMMAND_RESULT:
				/* close problem tasks will never have 'in progress' status */
				if (SUCCEED == tm_process_remote_command_result(taskid))
					processed_num++;
				break;
			case TRX_TM_TASK_ACKNOWLEDGE:
				trx_vector_uint64_append(&ack_taskids, taskid);
				break;
			case TRX_TM_TASK_CHECK_NOW:
				if (0 != ttl && clock + ttl < now)
					trx_vector_uint64_append(&expire_taskids, taskid);
				else
					trx_vector_uint64_append(&check_now_taskids, taskid);
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
				break;
		}

	}
	DBfree_result(result);

	if (0 < ack_taskids.values_num)
		processed_num += tm_process_acknowledgements(&ack_taskids);

	if (0 < check_now_taskids.values_num)
		processed_num += tm_process_check_now(&check_now_taskids);

	if (0 < expire_taskids.values_num)
		expired_num += tm_expire_generic_tasks(&expire_taskids);

	trx_vector_uint64_destroy(&expire_taskids);
	trx_vector_uint64_destroy(&check_now_taskids);
	trx_vector_uint64_destroy(&ack_taskids);

	return processed_num + expired_num;
}

/******************************************************************************
 *                                                                            *
 * Function: tm_remove_old_tasks                                              *
 *                                                                            *
 * Purpose: remove old done/expired tasks                                     *
 *                                                                            *
 ******************************************************************************/
static void	tm_remove_old_tasks(int now)
{
	DBbegin();
	DBexecute("delete from task where status in (%d,%d) and clock<=%d",
			TRX_TM_STATUS_DONE, TRX_TM_STATUS_EXPIRED, now - TRX_TM_CLEANUP_TASK_AGE);
	DBcommit();
}

TRX_THREAD_ENTRY(taskmanager_thread, args)
{
	static int	cleanup_time = 0;
	double		sec1, sec2;
	int		tasks_num, sleeptime, nextcheck;

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	trx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));
	DBconnect(TRX_DB_CONNECT_NORMAL);

	if (SUCCEED == trx_is_export_enabled())
		trx_problems_export_init("task-manager", process_num);

	sec1 = trx_time();

	sleeptime = TRX_TM_PROCESS_PERIOD - (int)sec1 % TRX_TM_PROCESS_PERIOD;

	trx_setproctitle("%s [started, idle %d sec]", get_process_type_string(process_type), sleeptime);

	while (TRX_IS_RUNNING())
	{
		trx_sleep_loop(sleeptime);

		sec1 = trx_time();
		trx_update_env(sec1);

		trx_setproctitle("%s [processing tasks]", get_process_type_string(process_type));

		tasks_num = tm_process_tasks((int)sec1);
		if (TRX_TM_CLEANUP_PERIOD <= sec1 - cleanup_time)
		{
			tm_remove_old_tasks((int)sec1);
			cleanup_time = sec1;
		}

		sec2 = trx_time();

		nextcheck = (int)sec1 - (int)sec1 % TRX_TM_PROCESS_PERIOD + TRX_TM_PROCESS_PERIOD;

		if (0 > (sleeptime = nextcheck - (int)sec2))
			sleeptime = 0;

		trx_setproctitle("%s [processed %d task(s) in " TRX_FS_DBL " sec, idle %d sec]",
				get_process_type_string(process_type), tasks_num, sec2 - sec1, sleeptime);
	}

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);
}
