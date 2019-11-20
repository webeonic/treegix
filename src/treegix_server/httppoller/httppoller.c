

#include "common.h"

#include "db.h"
#include "log.h"
#include "daemon.h"
#include "trxself.h"

#include "httptest.h"
#include "httppoller.h"

extern int		CONFIG_HTTPPOLLER_FORKS;
extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

/******************************************************************************
 *                                                                            *
 * Function: get_minnextcheck                                                 *
 *                                                                            *
 * Purpose: calculate when we have to process earliest httptest               *
 *                                                                            *
 * Return value: timestamp of earliest check or -1 if not found               *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
static int	get_minnextcheck(void)
{
	DB_RESULT	result;
	DB_ROW		row;
	int		res;

	result = DBselect(
			"select min(t.nextcheck)"
			" from httptest t,hosts h"
			" where t.hostid=h.hostid"
				" and " TRX_SQL_MOD(t.httptestid,%d) "=%d"
				" and t.status=%d"
				" and h.proxy_hostid is null"
				" and h.status=%d"
				" and (h.maintenance_status=%d or h.maintenance_type=%d)",
			CONFIG_HTTPPOLLER_FORKS, process_num - 1,
			HTTPTEST_STATUS_MONITORED,
			HOST_STATUS_MONITORED,
			HOST_MAINTENANCE_STATUS_OFF, MAINTENANCE_TYPE_NORMAL);

	if (NULL == (row = DBfetch(result)) || SUCCEED == DBis_null(row[0]))
	{
		treegix_log(LOG_LEVEL_DEBUG, "No httptests to process in get_minnextcheck.");
		res = FAIL;
	}
	else
		res = atoi(row[0]);

	DBfree_result(result);

	return res;
}

/******************************************************************************
 *                                                                            *
 * Function: main_httppoller_loop                                             *
 *                                                                            *
 * Purpose: main loop of processing of httptests                              *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: never returns                                                    *
 *                                                                            *
 ******************************************************************************/
TRX_THREAD_ENTRY(httppoller_thread, args)
{
	int	now, nextcheck, sleeptime = -1, httptests_count = 0, old_httptests_count = 0;
	double	sec, total_sec = 0.0, old_total_sec = 0.0;
	time_t	last_stat_time;

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

#define STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

	trx_setproctitle("%s #%d [connecting to the database]", get_process_type_string(process_type), process_num);
	last_stat_time = time(NULL);

	DBconnect(TRX_DB_CONNECT_NORMAL);

	while (TRX_IS_RUNNING())
	{
		sec = trx_time();
		trx_update_env(sec);

		if (0 != sleeptime)
		{
			trx_setproctitle("%s #%d [got %d values in " TRX_FS_DBL " sec, getting values]",
					get_process_type_string(process_type), process_num, old_httptests_count,
					old_total_sec);
		}

		now = time(NULL);
		httptests_count += process_httptests(process_num, now);
		total_sec += trx_time() - sec;

		nextcheck = get_minnextcheck();
		sleeptime = calculate_sleeptime(nextcheck, POLLER_DELAY);

		if (0 != sleeptime || STAT_INTERVAL <= time(NULL) - last_stat_time)
		{
			if (0 == sleeptime)
			{
				trx_setproctitle("%s #%d [got %d values in " TRX_FS_DBL " sec, getting values]",
						get_process_type_string(process_type), process_num, httptests_count,
						total_sec);
			}
			else
			{
				trx_setproctitle("%s #%d [got %d values in " TRX_FS_DBL " sec, idle %d sec]",
						get_process_type_string(process_type), process_num, httptests_count,
						total_sec, sleeptime);
				old_httptests_count = httptests_count;
				old_total_sec = total_sec;
			}
			httptests_count = 0;
			total_sec = 0.0;
			last_stat_time = time(NULL);
		}

		trx_sleep_loop(sleeptime);
	}

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);
#undef STAT_INTERVAL
}
