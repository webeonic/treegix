

#include "common.h"

#include "db.h"
#include "log.h"
#include "daemon.h"
#include "trxself.h"

#include "dbcache.h"
#include "dbsyncer.h"
#include "export.h"

extern int		CONFIG_HISTSYNCER_FREQUENCY;
extern unsigned char	process_type, program_type;
extern int		server_num, process_num;
static sigset_t		orig_mask;

/******************************************************************************
 *                                                                            *
 * Function: block_signals                                                    *
 *                                                                            *
 * Purpose: block signals to avoid interruption                               *
 *                                                                            *
 ******************************************************************************/
static	void	block_signals(void)
{
	sigset_t	mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	sigaddset(&mask, SIGUSR2);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGQUIT);

	if (0 > sigprocmask(SIG_BLOCK, &mask, &orig_mask))
		treegix_log(LOG_LEVEL_WARNING, "cannot set sigprocmask to block the signal");
}

/******************************************************************************
 *                                                                            *
 * Function: unblock_signals                                                  *
 *                                                                            *
 * Purpose: unblock signals after blocking                                    *
 *                                                                            *
 ******************************************************************************/
static	void	unblock_signals(void)
{
	if (0 > sigprocmask(SIG_SETMASK, &orig_mask, NULL))
		treegix_log(LOG_LEVEL_WARNING,"cannot restore sigprocmask");
}

/******************************************************************************
 *                                                                            *
 * Function: main_dbsyncer_loop                                               *
 *                                                                            *
 * Purpose: periodically synchronises data in memory cache with database      *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: never returns                                                    *
 *                                                                            *
 ******************************************************************************/
TRX_THREAD_ENTRY(dbsyncer_thread, args)
{
	int		sleeptime = -1, total_values_num = 0, values_num, more, total_triggers_num = 0, triggers_num;
	double		sec, total_sec = 0.0;
	time_t		last_stat_time;
	char		*stats = NULL;
	const char	*process_name;
	size_t		stats_alloc = 0, stats_offset = 0;

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type), server_num,
			(process_name = get_process_type_string(process_type)), process_num);

#define STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

	trx_setproctitle("%s #%d [connecting to the database]", process_name, process_num);
	last_stat_time = time(NULL);

	trx_strcpy_alloc(&stats, &stats_alloc, &stats_offset, "started");

	/* database APIs might not handle signals correctly and hang, block signals to avoid hanging */
	block_signals();
	DBconnect(TRX_DB_CONNECT_NORMAL);
	unblock_signals();

	if (SUCCEED == trx_is_export_enabled())
	{
		trx_history_export_init("history-syncer", process_num);
		trx_problems_export_init("history-syncer", process_num);
	}

	for (;;)
	{
		sec = trx_time();
		trx_update_env(sec);

		if (0 != sleeptime)
			trx_setproctitle("%s #%d [%s, syncing history]", process_name, process_num, stats);

		/* clear timer trigger queue to avoid processing time triggers at exit */
		if (!TRX_IS_RUNNING())
		{
			trx_dc_clear_timer_queue();
			trx_log_sync_history_cache_progress();
		}

		/* database APIs might not handle signals correctly and hang, block signals to avoid hanging */
		block_signals();
		trx_sync_history_cache(&values_num, &triggers_num, &more);
		unblock_signals();

		total_values_num += values_num;
		total_triggers_num += triggers_num;
		total_sec += trx_time() - sec;

		sleeptime = (TRX_SYNC_MORE == more ? 0 : CONFIG_HISTSYNCER_FREQUENCY);

		if (0 != sleeptime || STAT_INTERVAL <= time(NULL) - last_stat_time)
		{
			stats_offset = 0;
			trx_snprintf_alloc(&stats, &stats_alloc, &stats_offset, "processed %d values", total_values_num);

			if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
			{
				trx_snprintf_alloc(&stats, &stats_alloc, &stats_offset, ", %d triggers",
						total_triggers_num);
			}

			trx_snprintf_alloc(&stats, &stats_alloc, &stats_offset, " in " TRX_FS_DBL " sec", total_sec);

			if (0 == sleeptime)
				trx_setproctitle("%s #%d [%s, syncing history]", process_name, process_num, stats);
			else
				trx_setproctitle("%s #%d [%s, idle %d sec]", process_name, process_num, stats, sleeptime);

			total_values_num = 0;
			total_triggers_num = 0;
			total_sec = 0.0;
			last_stat_time = time(NULL);
		}

		if (TRX_SYNC_MORE == more)
			continue;

		if (!TRX_IS_RUNNING())
			break;

		trx_sleep_loop(sleeptime);
	}

	trx_log_sync_history_cache_progress();

	trx_free(stats);
	DBclose();
	exit(EXIT_SUCCESS);
#undef STAT_INTERVAL
}
