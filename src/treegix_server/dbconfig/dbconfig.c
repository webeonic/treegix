

#include "common.h"

#include "db.h"
#include "daemon.h"
#include "trxself.h"
#include "log.h"
#include "dbconfig.h"
#include "dbcache.h"

extern int		CONFIG_CONFSYNCER_FREQUENCY;
extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

static void	trx_dbconfig_sigusr_handler(int flags)
{
	if (TRX_RTC_CONFIG_CACHE_RELOAD == TRX_RTC_GET_MSG(flags))
	{
		if (0 < trx_sleep_get_remainder())
		{
			treegix_log(LOG_LEVEL_WARNING, "forced reloading of the configuration cache");
			trx_wakeup();
		}
		else
			treegix_log(LOG_LEVEL_WARNING, "configuration cache reloading is already in progress");
	}
}

/******************************************************************************
 *                                                                            *
 * Function: main_dbconfig_loop                                               *
 *                                                                            *
 * Purpose: periodically synchronises database data with memory cache         *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments: never returns                                                    *
 *                                                                            *
 ******************************************************************************/
TRX_THREAD_ENTRY(dbconfig_thread, args)
{
	double	sec = 0.0;

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	trx_setproctitle("%s [waiting %d sec for processes]", get_process_type_string(process_type),
			CONFIG_CONFSYNCER_FREQUENCY);

	trx_set_sigusr_handler(trx_dbconfig_sigusr_handler);

	/* the initial configuration sync is done by server before worker processes are forked */
	trx_sleep_loop(CONFIG_CONFSYNCER_FREQUENCY);

	trx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));

	DBconnect(TRX_DB_CONNECT_NORMAL);

	while (TRX_IS_RUNNING())
	{
		trx_setproctitle("%s [synced configuration in " TRX_FS_DBL " sec, syncing configuration]",
				get_process_type_string(process_type), sec);

		sec = trx_time();
		trx_update_env(sec);

		DCsync_configuration(TRX_DBSYNC_UPDATE);
		DCupdate_hosts_availability();
		sec = trx_time() - sec;

		trx_setproctitle("%s [synced configuration in " TRX_FS_DBL " sec, idle %d sec]",
				get_process_type_string(process_type), sec, CONFIG_CONFSYNCER_FREQUENCY);

		trx_sleep_loop(CONFIG_CONFSYNCER_FREQUENCY);
	}

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);
}
