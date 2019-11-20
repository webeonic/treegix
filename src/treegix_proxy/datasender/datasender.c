

#include "common.h"
#include "comms.h"
#include "db.h"
#include "log.h"
#include "daemon.h"
#include "trxjson.h"
#include "proxy.h"
#include "trxself.h"
#include "dbcache.h"
#include "trxtasks.h"
#include "dbcache.h"

#include "datasender.h"
#include "../servercomms.h"
#include "../../libs/trxcrypto/tls.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

#define TRX_DATASENDER_AVAILABILITY		0x0001
#define TRX_DATASENDER_HISTORY			0x0002
#define TRX_DATASENDER_DISCOVERY		0x0004
#define TRX_DATASENDER_AUTOREGISTRATION		0x0008
#define TRX_DATASENDER_TASKS			0x0010
#define TRX_DATASENDER_TASKS_RECV		0x0020
#define TRX_DATASENDER_TASKS_REQUEST		0x8000

#define TRX_DATASENDER_DB_UPDATE	(TRX_DATASENDER_HISTORY | TRX_DATASENDER_DISCOVERY |		\
					TRX_DATASENDER_AUTOREGISTRATION | TRX_DATASENDER_TASKS |	\
					TRX_DATASENDER_TASKS_RECV)

/******************************************************************************
 *                                                                            *
 * Function: proxy_data_sender                                                *
 *                                                                            *
 * Purpose: collects host availability, history, discovery, auto registration *
 *          data and sends 'proxy data' request                               *
 *                                                                            *
 ******************************************************************************/
static int	proxy_data_sender(int *more, int now)
{
	static int		data_timestamp = 0, task_timestamp = 0, upload_state = SUCCEED;

	trx_socket_t		sock;
	struct trx_json		j;
	struct trx_json_parse	jp, jp_tasks;
	int			availability_ts, history_records = 0, discovery_records = 0,
				areg_records = 0, more_history = 0, more_discovery = 0, more_areg = 0;
	trx_uint64_t		history_lastid = 0, discovery_lastid = 0, areg_lastid = 0, flags = 0;
	trx_timespec_t		ts;
	char			*error = NULL;
	trx_vector_ptr_t	tasks;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	*more = TRX_PROXY_DATA_DONE;
	trx_json_init(&j, 16 * TRX_KIBIBYTE);

	trx_json_addstring(&j, TRX_PROTO_TAG_REQUEST, TRX_PROTO_VALUE_PROXY_DATA, TRX_JSON_TYPE_STRING);
	trx_json_addstring(&j, TRX_PROTO_TAG_HOST, CONFIG_HOSTNAME, TRX_JSON_TYPE_STRING);
	trx_json_addstring(&j, TRX_PROTO_TAG_SESSION, trx_dc_get_session_token(), TRX_JSON_TYPE_STRING);

	if (SUCCEED == upload_state && CONFIG_PROXYDATA_FREQUENCY <= now - data_timestamp)
	{
		if (SUCCEED == get_host_availability_data(&j, &availability_ts))
			flags |= TRX_DATASENDER_AVAILABILITY;

		history_records = proxy_get_hist_data(&j, &history_lastid, &more_history);
		if (0 != history_lastid)
			flags |= TRX_DATASENDER_HISTORY;

		discovery_records = proxy_get_dhis_data(&j, &discovery_lastid, &more_discovery);
		if (0 != discovery_records)
			flags |= TRX_DATASENDER_DISCOVERY;

		areg_records = proxy_get_areg_data(&j, &areg_lastid, &more_areg);
		if (0 != areg_records)
			flags |= TRX_DATASENDER_AUTOREGISTRATION;

		if (TRX_PROXY_DATA_MORE != more_history && TRX_PROXY_DATA_MORE != more_discovery &&
						TRX_PROXY_DATA_MORE != more_areg)
		{
			data_timestamp = now;
		}
	}

	trx_vector_ptr_create(&tasks);

	if (SUCCEED == upload_state && TRX_TASK_UPDATE_FREQUENCY <= now - task_timestamp)
	{
		task_timestamp = now;

		trx_tm_get_remote_tasks(&tasks, 0);

		if (0 != tasks.values_num)
		{
			trx_tm_json_serialize_tasks(&j, &tasks);
			flags |= TRX_DATASENDER_TASKS;
		}

		flags |= TRX_DATASENDER_TASKS_REQUEST;
	}

	if (SUCCEED != upload_state)
		flags |= TRX_DATASENDER_TASKS_REQUEST;

	if (0 != flags)
	{
		if (TRX_PROXY_DATA_MORE == more_history || TRX_PROXY_DATA_MORE == more_discovery ||
				TRX_PROXY_DATA_MORE == more_areg)
		{
			trx_json_adduint64(&j, TRX_PROTO_TAG_MORE, TRX_PROXY_DATA_MORE);
			*more = TRX_PROXY_DATA_MORE;
		}

		trx_json_addstring(&j, TRX_PROTO_TAG_VERSION, TREEGIX_VERSION, TRX_JSON_TYPE_STRING);

		/* retry till have a connection */
		if (FAIL == connect_to_server(&sock, 600, CONFIG_PROXYDATA_FREQUENCY))
			goto clean;

		trx_timespec(&ts);
		trx_json_adduint64(&j, TRX_PROTO_TAG_CLOCK, ts.sec);
		trx_json_adduint64(&j, TRX_PROTO_TAG_NS, ts.ns);

		if (SUCCEED != (upload_state = put_data_to_server(&sock, &j, &error)))
		{
			*more = TRX_PROXY_DATA_DONE;
			treegix_log(LOG_LEVEL_WARNING, "cannot send proxy data to server at \"%s\": %s",
					sock.peer, error);
			trx_free(error);
		}
		else
		{
			if (0 != (flags & TRX_DATASENDER_AVAILABILITY))
				trx_set_availability_diff_ts(availability_ts);

			if (SUCCEED == trx_json_open(sock.buffer, &jp))
			{
				if (SUCCEED == trx_json_brackets_by_name(&jp, TRX_PROTO_TAG_TASKS, &jp_tasks))
					flags |= TRX_DATASENDER_TASKS_RECV;
			}

			if (0 != (flags & TRX_DATASENDER_DB_UPDATE))
			{
				DBbegin();

				if (0 != (flags & TRX_DATASENDER_TASKS))
				{
					trx_tm_update_task_status(&tasks, TRX_TM_STATUS_DONE);
					trx_vector_ptr_clear_ext(&tasks, (trx_clean_func_t)trx_tm_task_free);
				}

				if (0 != (flags & TRX_DATASENDER_TASKS_RECV))
				{
					trx_tm_json_deserialize_tasks(&jp_tasks, &tasks);
					trx_tm_save_tasks(&tasks);
				}

				if (0 != (flags & TRX_DATASENDER_HISTORY))
					proxy_set_hist_lastid(history_lastid);

				if (0 != (flags & TRX_DATASENDER_DISCOVERY))
					proxy_set_dhis_lastid(discovery_lastid);

				if (0 != (flags & TRX_DATASENDER_AUTOREGISTRATION))
					proxy_set_areg_lastid(areg_lastid);

				DBcommit();
			}
		}

		disconnect_server(&sock);
	}
clean:
	trx_vector_ptr_clear_ext(&tasks, (trx_clean_func_t)trx_tm_task_free);
	trx_vector_ptr_destroy(&tasks);

	trx_json_free(&j);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s more:%d flags:0x" TRX_FS_UX64, __func__,
			trx_result_string(upload_state), *more, flags);

	return history_records + discovery_records + areg_records;
}

/******************************************************************************
 *                                                                            *
 * Function: main_datasender_loop                                             *
 *                                                                            *
 * Purpose: periodically sends history and events to the server               *
 *                                                                            *
 ******************************************************************************/
TRX_THREAD_ENTRY(datasender_thread, args)
{
	int		records = 0, more;
	double		time_start, time_diff = 0.0, time_now;

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	trx_tls_init_child();
#endif
	trx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));

	DBconnect(TRX_DB_CONNECT_NORMAL);

	while (TRX_IS_RUNNING())
	{
		time_now = trx_time();
		trx_update_env(time_now);

		trx_setproctitle("%s [sent %d values in " TRX_FS_DBL " sec, sending data]",
				get_process_type_string(process_type), records, time_diff);

		records = 0;
		time_start = time_now;

		do
		{
			records += proxy_data_sender(&more, (int)time_now);

			time_now = trx_time();
			time_diff = time_now - time_start;
		}
		while (TRX_PROXY_DATA_MORE == more && time_diff < SEC_PER_MIN && TRX_IS_RUNNING());

		trx_setproctitle("%s [sent %d values in " TRX_FS_DBL " sec, idle %d sec]",
				get_process_type_string(process_type), records, time_diff,
				TRX_PROXY_DATA_MORE != more ? TRX_TASK_UPDATE_FREQUENCY : 0);

		if (TRX_PROXY_DATA_MORE != more)
			trx_sleep_loop(TRX_TASK_UPDATE_FREQUENCY);
	}

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);
}
