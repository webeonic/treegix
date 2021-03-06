

#include "common.h"
#include "daemon.h"
#include "db.h"
#include "log.h"
#include "trxipcservice.h"
#include "trxself.h"
#include "dbcache.h"
#include "proxy.h"
#include "../events.h"

#include "lld_worker.h"
#include "lld_protocol.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

/******************************************************************************
 *                                                                            *
 * Function: lld_register_worker                                              *
 *                                                                            *
 * Purpose: registers lld worker with lld manager                             *
 *                                                                            *
 * Parameters: socket - [IN] the connections socket                           *
 *                                                                            *
 ******************************************************************************/
static void	lld_register_worker(trx_ipc_socket_t *socket)
{
	pid_t	ppid;

	ppid = getppid();

	trx_ipc_socket_write(socket, TRX_IPC_LLD_REGISTER, (unsigned char *)&ppid, sizeof(ppid));
}

/******************************************************************************
 *                                                                            *
 * Function: lld_process_task                                                 *
 *                                                                            *
 * Purpose: processes lld task and updates rule state/error in configuration  *
 *          cache and database                                                *
 *                                                                            *
 * Parameters: message - [IN] the message with LLD request                    *
 *                                                                            *
 ******************************************************************************/
static void	lld_process_task(trx_ipc_message_t *message)
{
	trx_uint64_t		itemid, lastlogsize;
	char			*value, *error;
	trx_timespec_t		ts;
	trx_item_diff_t		diff;
	DC_ITEM			item;
	int			errcode, mtime;
	unsigned char		state, meta;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_lld_deserialize_item_value(message->data, &itemid, &value, &ts, &meta, &lastlogsize, &mtime, &error);

	DCconfig_get_items_by_itemids(&item, &itemid, &errcode, 1);
	if (SUCCEED != errcode)
		goto out;

	treegix_log(LOG_LEVEL_DEBUG, "processing discovery rule:" TRX_FS_UI64, itemid);

	diff.flags = TRX_FLAGS_ITEM_DIFF_UNSET;

	if (NULL != error || NULL != value)
	{
		if (NULL == error && SUCCEED == lld_process_discovery_rule(itemid, value, &error))
			state = ITEM_STATE_NORMAL;
		else
			state = ITEM_STATE_NOTSUPPORTED;

		if (state != item.state)
		{
			diff.state = state;
			diff.flags |= TRX_FLAGS_ITEM_DIFF_UPDATE_STATE;

			if (ITEM_STATE_NORMAL == state)
			{
				treegix_log(LOG_LEVEL_WARNING, "discovery rule \"%s:%s\" became supported",
						item.host.host, item.key_orig);

				trx_add_event(EVENT_SOURCE_INTERNAL, EVENT_OBJECT_LLDRULE, itemid, &ts,
						ITEM_STATE_NORMAL, NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0, NULL,
						NULL);
			}
			else
			{
				treegix_log(LOG_LEVEL_WARNING, "discovery rule \"%s:%s\" became not supported: %s",
						item.host.host, item.key_orig, error);

				trx_add_event(EVENT_SOURCE_INTERNAL, EVENT_OBJECT_LLDRULE, itemid, &ts,
						ITEM_STATE_NOTSUPPORTED, NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0,
						NULL, error);
			}

			trx_process_events(NULL, NULL);
			trx_clean_events();
		}

		/* with successful LLD processing LLD error will be set to empty string */
		if (NULL != error && 0 != strcmp(error, item.error))
		{
			diff.error = error;
			diff.flags |= TRX_FLAGS_ITEM_DIFF_UPDATE_ERROR;
		}
	}

	if (0 != meta)
	{
		if (item.lastlogsize != lastlogsize)
		{
			diff.lastlogsize = lastlogsize;
			diff.flags |= TRX_FLAGS_ITEM_DIFF_UPDATE_LASTLOGSIZE;
		}
		if (item.mtime != mtime)
		{
			diff.mtime = mtime;
			diff.flags |= TRX_FLAGS_ITEM_DIFF_UPDATE_MTIME;
		}
	}

	if (TRX_FLAGS_ITEM_DIFF_UNSET != diff.flags)
	{
		trx_vector_ptr_t	diffs;
		char			*sql = NULL;
		size_t			sql_alloc = 0, sql_offset = 0;

		trx_vector_ptr_create(&diffs);
		diff.itemid = itemid;
		trx_vector_ptr_append(&diffs, &diff);

		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);
		trx_db_save_item_changes(&sql, &sql_alloc, &sql_offset, &diffs);
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);
		if (16 < sql_offset)
			DBexecute("%s", sql);

		DCconfig_items_apply_changes(&diffs);

		trx_vector_ptr_destroy(&diffs);
		trx_free(sql);
	}

	DCconfig_clean_items(&item, &errcode, 1);
out:
	trx_free(value);
	trx_free(error);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}


TRX_THREAD_ENTRY(lld_worker_thread, args)
{
#define	STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

	char			*error = NULL;
	trx_ipc_socket_t	lld_socket;
	trx_ipc_message_t	message;
	double			time_stat, time_idle = 0, time_now, time_read;
	trx_uint64_t		processed_num = 0;

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	trx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));

	trx_ipc_message_init(&message);

	if (FAIL == trx_ipc_socket_open(&lld_socket, TRX_IPC_SERVICE_LLD, SEC_PER_MIN, &error))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot connect to lld manager service: %s", error);
		trx_free(error);
		exit(EXIT_FAILURE);
	}

	lld_register_worker(&lld_socket);

	time_stat = trx_time();


	DBconnect(TRX_DB_CONNECT_NORMAL);

	trx_setproctitle("%s #%d started", get_process_type_string(process_type), process_num);

	update_selfmon_counter(TRX_PROCESS_STATE_BUSY);

	while (TRX_IS_RUNNING())
	{
		time_now = trx_time();

		if (STAT_INTERVAL < time_now - time_stat)
		{
			trx_setproctitle("%s #%d [processed " TRX_FS_UI64 " LLD rules, idle " TRX_FS_DBL " sec during "
					TRX_FS_DBL " sec]", get_process_type_string(process_type), process_num,
					processed_num, time_idle, time_now - time_stat);

			time_stat = time_now;
			time_idle = 0;
			processed_num = 0;
		}

		update_selfmon_counter(TRX_PROCESS_STATE_IDLE);
		if (SUCCEED != trx_ipc_socket_read(&lld_socket, &message))
		{
			treegix_log(LOG_LEVEL_CRIT, "cannot read LLD manager service request");
			exit(EXIT_FAILURE);
		}
		update_selfmon_counter(TRX_PROCESS_STATE_BUSY);

		time_read = trx_time();
		time_idle += time_read - time_now;
		trx_update_env(time_read);

		switch (message.code)
		{
			case TRX_IPC_LLD_TASK:
				lld_process_task(&message);
				trx_ipc_socket_write(&lld_socket, TRX_IPC_LLD_DONE, NULL, 0);
				processed_num++;
				break;
		}

		trx_ipc_message_clean(&message);
	}

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);

	DBclose();

	trx_ipc_socket_close(&lld_socket);
}
