

#include <assert.h>

#include "common.h"
#include "log.h"

#include "db.h"
#include "trxjson.h"
#include "trxtasks.h"

/******************************************************************************
 *                                                                            *
 * Function: trx_tm_get_remote_tasks                                          *
 *                                                                            *
 * Purpose: get tasks scheduled to be executed on the server                  *
 *                                                                            *
 * Parameters: tasks        - [OUT] the tasks to execute                      *
 *             proxy_hostid - [IN] (ignored)                                  *
 *                                                                            *
 * Comments: This function is used by proxy to get tasks to be sent to the    *
 *           server.                                                          *
 *                                                                            *
 ******************************************************************************/
void	trx_tm_get_remote_tasks(trx_vector_ptr_t *tasks, trx_uint64_t proxy_hostid)
{
	DB_RESULT	result;
	DB_ROW		row;

	TRX_UNUSED(proxy_hostid);

	result = DBselect(
			"select t.taskid,t.type,t.clock,t.ttl,"
				"r.status,r.parent_taskid,r.info"
			" from task t,task_remote_command_result r"
			" where t.taskid=r.taskid"
				" and t.status=%d"
				" and t.type=%d"
			" order by t.taskid",
			TRX_TM_STATUS_NEW, TRX_TM_TASK_REMOTE_COMMAND_RESULT);

	while (NULL != (row = DBfetch(result)))
	{
		trx_uint64_t	taskid, parent_taskid;
		trx_tm_task_t	*task;

		TRX_STR2UINT64(taskid, row[0]);
		TRX_DBROW2UINT64(parent_taskid, row[5]);

		task = trx_tm_task_create(taskid, atoi(row[1]), TRX_TM_STATUS_NEW, atoi(row[2]), atoi(row[3]), 0);

		task->data = trx_tm_remote_command_result_create(parent_taskid, atoi(row[4]), row[6]);

		trx_vector_ptr_append(tasks, task);
	}

	DBfree_result(result);
}


