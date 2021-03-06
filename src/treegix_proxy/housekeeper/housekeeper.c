

#include "common.h"
#include "db.h"
#include "log.h"
#include "daemon.h"
#include "trxself.h"
#include "dbcache.h"

#include "housekeeper.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

static int	hk_period;

/* the maximum number of housekeeping periods to be removed per single housekeeping cycle */
#define HK_MAX_DELETE_PERIODS	4

static void	trx_housekeeper_sigusr_handler(int flags)
{
	if (TRX_RTC_HOUSEKEEPER_EXECUTE == TRX_RTC_GET_MSG(flags))
	{
		if (0 < trx_sleep_get_remainder())
		{
			treegix_log(LOG_LEVEL_WARNING, "forced execution of the housekeeper");
			trx_wakeup();
		}
		else
			treegix_log(LOG_LEVEL_WARNING, "housekeeping procedure is already in progress");
	}
}

/******************************************************************************
 *                                                                            *
 * Function: delete_history                                                   *
 *                                                                            *
 * Purpose: remove outdated information from historical table                 *
 *                                                                            *
 * Parameters: now - current timestamp                                        *
 *                                                                            *
 * Return value: number of rows records                                       *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
static int	delete_history(const char *table, const char *fieldname, int now)
{
	DB_RESULT       result;
	DB_ROW          row;
	int             minclock, records = 0;
	trx_uint64_t	lastid, maxid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() table:'%s' now:%d", __func__, table, now);

	DBbegin();

	result = DBselect(
			"select nextid"
			" from ids"
			" where table_name='%s'"
				" and field_name='%s'",
			table, fieldname);

	if (NULL == (row = DBfetch(result)))
		goto rollback;

	TRX_STR2UINT64(lastid, row[0]);
	DBfree_result(result);

	result = DBselect("select min(clock) from %s",
			table);

	if (NULL == (row = DBfetch(result)) || SUCCEED == DBis_null(row[0]))
		goto rollback;

	minclock = atoi(row[0]);
	DBfree_result(result);

	result = DBselect("select max(id) from %s",
			table);

	if (NULL == (row = DBfetch(result)) || SUCCEED == DBis_null(row[0]))
		goto rollback;

	TRX_STR2UINT64(maxid, row[0]);
	DBfree_result(result);

	records = DBexecute(
			"delete from %s"
			" where id<" TRX_FS_UI64
				" and (clock<%d"
					" or (id<=" TRX_FS_UI64 " and clock<%d))",
			table, maxid,
			now - CONFIG_PROXY_OFFLINE_BUFFER * SEC_PER_HOUR,
			lastid,
			MIN(now - CONFIG_PROXY_LOCAL_BUFFER * SEC_PER_HOUR,
					minclock + HK_MAX_DELETE_PERIODS * hk_period));

	DBcommit();

	return records;
rollback:
	DBfree_result(result);

	DBrollback();

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: housekeeping_history                                             *
 *                                                                            *
 * Purpose: remove outdated information from history                          *
 *                                                                            *
 * Parameters: now - current timestamp                                        *
 *                                                                            *
 * Return value: SUCCEED - information removed successfully                   *
 *               FAIL - otherwise                                             *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
static int	housekeeping_history(int now)
{
        int	records = 0;

        treegix_log(LOG_LEVEL_DEBUG, "In housekeeping_history()");

	records += delete_history("proxy_history", "history_lastid", now);
	records += delete_history("proxy_dhistory", "dhistory_lastid", now);
	records += delete_history("proxy_autoreg_host", "autoreg_host_lastid", now);

        return records;
}

static int	get_housekeeper_period(double time_slept)
{
	if (SEC_PER_HOUR > time_slept)
		return SEC_PER_HOUR;
	else if (24 * SEC_PER_HOUR < time_slept)
		return 24 * SEC_PER_HOUR;
	else
		return (int)time_slept;
}

TRX_THREAD_ENTRY(housekeeper_thread, args)
{
	int	records, start, sleeptime;
	double	sec, time_slept, time_now;
	char	sleeptext[25];

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	if (0 == CONFIG_HOUSEKEEPING_FREQUENCY)
	{
		trx_setproctitle("%s [waiting for user command]", get_process_type_string(process_type));
		trx_snprintf(sleeptext, sizeof(sleeptext), "waiting for user command");
	}
	else
	{
		sleeptime = HOUSEKEEPER_STARTUP_DELAY * SEC_PER_MIN;
		trx_setproctitle("%s [startup idle for %d minutes]", get_process_type_string(process_type),
				HOUSEKEEPER_STARTUP_DELAY);
		trx_snprintf(sleeptext, sizeof(sleeptext), "idle for %d hour(s)", CONFIG_HOUSEKEEPING_FREQUENCY);
	}

	trx_set_sigusr_handler(trx_housekeeper_sigusr_handler);

	while (TRX_IS_RUNNING())
	{
		sec = trx_time();

		if (0 == CONFIG_HOUSEKEEPING_FREQUENCY)
			trx_sleep_forever();
		else
			trx_sleep_loop(sleeptime);

		if (!TRX_IS_RUNNING())
			break;

		time_now = trx_time();
		time_slept = time_now - sec;
		trx_update_env(time_now);

		hk_period = get_housekeeper_period(time_slept);

		start = time(NULL);

		treegix_log(LOG_LEVEL_WARNING, "executing housekeeper");

		trx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));

		DBconnect(TRX_DB_CONNECT_NORMAL);

		trx_setproctitle("%s [removing old history]", get_process_type_string(process_type));

		sec = trx_time();
		records = housekeeping_history(start);
		sec = trx_time() - sec;

		DBclose();

		trx_dc_cleanup_data_sessions();

		treegix_log(LOG_LEVEL_WARNING, "%s [deleted %d records in " TRX_FS_DBL " sec, %s]",
				get_process_type_string(process_type), records, sec, sleeptext);

		trx_setproctitle("%s [deleted %d records in " TRX_FS_DBL " sec, %s]",
				get_process_type_string(process_type), records, sec, sleeptext);

		if (0 != CONFIG_HOUSEKEEPING_FREQUENCY)
			sleeptime = CONFIG_HOUSEKEEPING_FREQUENCY * SEC_PER_HOUR;
	}

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);
}
