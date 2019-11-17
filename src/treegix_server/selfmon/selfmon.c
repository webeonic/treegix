

#include "common.h"
#include "daemon.h"
#include "zbxself.h"
#include "log.h"
#include "selfmon.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

TRX_THREAD_ENTRY(selfmon_thread, args)
{
	double	sec;

	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	while (TRX_IS_RUNNING())
	{
		sec = zbx_time();
		zbx_update_env(sec);

		zbx_setproctitle("%s [processing data]", get_process_type_string(process_type));

		collect_selfmon_stats();
		sec = zbx_time() - sec;

		zbx_setproctitle("%s [processed data in " TRX_FS_DBL " sec, idle 1 sec]",
				get_process_type_string(process_type), sec);

		zbx_sleep_loop(TRX_SELFMON_DELAY);
	}

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
}
