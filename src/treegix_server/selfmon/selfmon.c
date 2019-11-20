

#include "common.h"
#include "daemon.h"
#include "trxself.h"
#include "log.h"
#include "selfmon.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

TRX_THREAD_ENTRY(selfmon_thread, args)
{
	double	sec;

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	while (TRX_IS_RUNNING())
	{
		sec = trx_time();
		trx_update_env(sec);

		trx_setproctitle("%s [processing data]", get_process_type_string(process_type));

		collect_selfmon_stats();
		sec = trx_time() - sec;

		trx_setproctitle("%s [processed data in " TRX_FS_DBL " sec, idle 1 sec]",
				get_process_type_string(process_type), sec);

		trx_sleep_loop(TRX_SELFMON_DELAY);
	}

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);
}
