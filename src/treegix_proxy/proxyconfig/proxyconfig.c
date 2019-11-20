

#include "common.h"
#include "db.h"
#include "log.h"
#include "daemon.h"
#include "proxy.h"
#include "trxself.h"

#include "proxyconfig.h"
#include "../servercomms.h"
#include "../../libs/trxcrypto/tls.h"

#define CONFIG_PROXYCONFIG_RETRY	120	/* seconds */

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

static void	trx_proxyconfig_sigusr_handler(int flags)
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
 * Function: process_configuration_sync                                       *
 *                                                                            *
 ******************************************************************************/
static void	process_configuration_sync(size_t *data_size)
{
	trx_socket_t	sock;
	struct		trx_json_parse jp;
	char		value[16], *error = NULL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	/* reset the performance metric */
	*data_size = 0;

	if (FAIL == connect_to_server(&sock, 600, CONFIG_PROXYCONFIG_RETRY))	/* retry till have a connection */
		goto out;

	if (SUCCEED != get_data_from_server(&sock, TRX_PROTO_VALUE_PROXY_CONFIG, &error))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot obtain configuration data from server at \"%s\": %s",
				sock.peer, error);
		goto error;
	}

	if ('\0' == *sock.buffer)
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot obtain configuration data from server at \"%s\": %s",
				sock.peer, "empty string received");
		goto error;
	}

	if (SUCCEED != trx_json_open(sock.buffer, &jp))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot obtain configuration data from server at \"%s\": %s",
				sock.peer, trx_json_strerror());
		goto error;
	}

	*data_size = (size_t)(jp.end - jp.start + 1);     /* performance metric */

	/* if the answer is short then most likely it is a negative answer "response":"failed" */
	if (128 > *data_size &&
			SUCCEED == trx_json_value_by_name(&jp, TRX_PROTO_TAG_RESPONSE, value, sizeof(value)) &&
			0 == strcmp(value, TRX_PROTO_VALUE_FAILED))
	{
		char	*info = NULL;
		size_t	info_alloc = 0;

		if (SUCCEED != trx_json_value_by_name_dyn(&jp, TRX_PROTO_TAG_INFO, &info, &info_alloc))
			info = trx_dsprintf(info, "negative response \"%s\"", value);

		treegix_log(LOG_LEVEL_WARNING, "cannot obtain configuration data from server at \"%s\": %s",
				sock.peer, info);
		trx_free(info);
		goto error;
	}

	treegix_log(LOG_LEVEL_WARNING, "received configuration data from server at \"%s\", datalen " TRX_FS_SIZE_T,
			sock.peer, (trx_fs_size_t)*data_size);

	process_proxyconfig(&jp);
error:
	disconnect_server(&sock);

	trx_free(error);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: main_proxyconfig_loop                                            *
 *                                                                            *
 * Purpose: periodically request config data                                  *
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
TRX_THREAD_ENTRY(proxyconfig_thread, args)
{
	size_t	data_size;
	double	sec;

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

	trx_set_sigusr_handler(trx_proxyconfig_sigusr_handler);

	while (TRX_IS_RUNNING())
	{
		sec = trx_time();
		trx_update_env(sec);

		trx_setproctitle("%s [loading configuration]", get_process_type_string(process_type));

		process_configuration_sync(&data_size);
		sec = trx_time() - sec;

		trx_setproctitle("%s [synced config " TRX_FS_SIZE_T " bytes in " TRX_FS_DBL " sec, idle %d sec]",
				get_process_type_string(process_type), (trx_fs_size_t)data_size, sec,
				CONFIG_PROXYCONFIG_FREQUENCY);

		trx_sleep_loop(CONFIG_PROXYCONFIG_FREQUENCY);
	}

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);
}
