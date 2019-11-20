

#include "common.h"

#include "cfg.h"
#include "db.h"
#include "log.h"
#include "daemon.h"
#include "trxmedia.h"
#include "trxserver.h"
#include "trxself.h"
#include "trxexec.h"
#include "trxipcservice.h"

#include "alerter.h"
#include "alerter_protocol.h"
#include "alert_manager.h"
#include "trxembed.h"

#define	ALARM_ACTION_TIMEOUT	40

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

static trx_es_t	es_engine;

/******************************************************************************
 *                                                                            *
 * Function: execute_script_alert                                             *
 *                                                                            *
 * Purpose: execute script alert type                                         *
 *                                                                            *
 ******************************************************************************/
static int	execute_script_alert(const char *command, char *error, size_t max_error_len)
{
	char	*output = NULL;
	int	ret = FAIL;

	if (SUCCEED == (ret = trx_execute(command, &output, error, max_error_len, ALARM_ACTION_TIMEOUT,
			TRX_EXIT_CODE_CHECKS_ENABLED)))
	{
		treegix_log(LOG_LEVEL_DEBUG, "%s output:\n%s", command, output);
		trx_free(output);
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: alerter_register                                                 *
 *                                                                            *
 * Purpose: registers alerter with alert manager                              *
 *                                                                            *
 * Parameters: socket - [IN] the connections socket                           *
 *                                                                            *
 ******************************************************************************/
static void	alerter_register(trx_ipc_socket_t *socket)
{
	pid_t	ppid;

	ppid = getppid();

	trx_ipc_socket_write(socket, TRX_IPC_ALERTER_REGISTER, (unsigned char *)&ppid, sizeof(ppid));
}

/******************************************************************************
 *                                                                            *
 * Function: alerter_send_result                                              *
 *                                                                            *
 * Purpose: sends alert sending result to alert manager                       *
 *                                                                            *
 * Parameters: socket  - [IN] the connections socket                          *
 *             errcode - [IN] the error code                                  *
 *             value   - [IN] the value or error message                      *
 *                                                                            *
 ******************************************************************************/
static void	alerter_send_result(trx_ipc_socket_t *socket, const char *value, int errcode, const char *error)
{
	unsigned char	*data;
	trx_uint32_t	data_len;

	data_len = trx_alerter_serialize_result(&data, value, errcode, error);
	trx_ipc_socket_write(socket, TRX_IPC_ALERTER_RESULT, data, data_len);

	trx_free(data);
}

/******************************************************************************
 *                                                                            *
 * Function: alerter_process_email                                            *
 *                                                                            *
 * Purpose: processes email alert                                             *
 *                                                                            *
 * Parameters: socket      - [IN] the connections socket                      *
 *             ipc_message - [IN] the ipc message with media type and alert   *
 *                                data                                        *
 *                                                                            *
 ******************************************************************************/
static void	alerter_process_email(trx_ipc_socket_t *socket, trx_ipc_message_t *ipc_message)
{
	trx_uint64_t	alertid;
	char		*sendto, *subject, *message, *smtp_server, *smtp_helo, *smtp_email, *username, *password;
	unsigned short	smtp_port;
	unsigned char	smtp_security, smtp_verify_peer, smtp_verify_host, smtp_authentication, content_type;
	int		ret;
	char		error[MAX_STRING_LEN];


	trx_alerter_deserialize_email(ipc_message->data, &alertid, &sendto, &subject, &message, &smtp_server,
			&smtp_port, &smtp_helo, &smtp_email, &smtp_security, &smtp_verify_peer, &smtp_verify_host,
			&smtp_authentication, &username, &password, &content_type);

	ret = send_email(smtp_server, smtp_port, smtp_helo, smtp_email, sendto, subject, message, smtp_security,
			smtp_verify_peer, smtp_verify_host, smtp_authentication, username, password, content_type,
			ALARM_ACTION_TIMEOUT, error, sizeof(error));

	alerter_send_result(socket, NULL, ret, (SUCCEED == ret ? NULL : error));

	trx_free(sendto);
	trx_free(subject);
	trx_free(message);
	trx_free(smtp_server);
	trx_free(smtp_helo);
	trx_free(smtp_email);
	trx_free(username);
	trx_free(password);
}

/******************************************************************************
 *                                                                            *
 * Function: alerter_process_sms                                              *
 *                                                                            *
 * Purpose: processes SMS alert                                               *
 *                                                                            *
 * Parameters: socket      - [IN] the connections socket                      *
 *             ipc_message - [IN] the ipc message with media type and alert   *
 *                                data                                        *
 *                                                                            *
 ******************************************************************************/
static void	alerter_process_sms(trx_ipc_socket_t *socket, trx_ipc_message_t *ipc_message)
{
	trx_uint64_t	alertid;
	char		*sendto, *message, *gsm_modem;
	int		ret;
	char		error[MAX_STRING_LEN];

	trx_alerter_deserialize_sms(ipc_message->data, &alertid, &sendto, &message, &gsm_modem);

	/* SMS uses its own timeouts */
	ret = send_sms(gsm_modem, sendto, message, error, sizeof(error));
	alerter_send_result(socket, NULL, ret, (SUCCEED == ret ? NULL : error));

	trx_free(sendto);
	trx_free(message);
	trx_free(gsm_modem);
}

/******************************************************************************
 *                                                                            *
 * Function: alerter_process_exec                                             *
 *                                                                            *
 * Purpose: processes script alert                                            *
 *                                                                            *
 * Parameters: socket      - [IN] the connections socket                      *
 *             ipc_message - [IN] the ipc message with media type and alert   *
 *                                data                                        *
 *                                                                            *
 ******************************************************************************/
static void	alerter_process_exec(trx_ipc_socket_t *socket, trx_ipc_message_t *ipc_message)
{
	trx_uint64_t	alertid;
	char		*command;
	int		ret;
	char		error[MAX_STRING_LEN];

	trx_alerter_deserialize_exec(ipc_message->data, &alertid, &command);

	ret = execute_script_alert(command, error, sizeof(error));
	alerter_send_result(socket, NULL, ret, (SUCCEED == ret ? NULL : error));

	trx_free(command);
}

/******************************************************************************
 *                                                                            *
 * Function: alerter_process_webhook                                          *
 *                                                                            *
 * Purpose: processes webhook alert                                           *
 *                                                                            *
 * Parameters: socket      - [IN] the connections socket                      *
 *             ipc_message - [IN] the ipc message with media type and alert   *
 *                                data                                        *
 *                                                                            *
 ******************************************************************************/
static void	alerter_process_webhook(trx_ipc_socket_t *socket, trx_ipc_message_t *ipc_message)
{
	char	*script_bin = NULL, *params = NULL, *error = NULL, *output = NULL;
	int	script_bin_sz, ret, timeout;

	trx_alerter_deserialize_webhook(ipc_message->data, &script_bin, &script_bin_sz, &timeout, &params);

		if (SUCCEED != (ret = trx_es_is_env_initialized(&es_engine)))
		ret = trx_es_init_env(&es_engine, &error);

	if (SUCCEED == ret)
	{
		trx_es_set_timeout(&es_engine, timeout);
		ret = trx_es_execute(&es_engine, NULL, script_bin, script_bin_sz, params, &output, &error);
	}

	if (SUCCEED == trx_es_fatal_error(&es_engine))
	{
		char	*errmsg = NULL;
		if (SUCCEED != trx_es_destroy_env(&es_engine, &errmsg))
		{
			treegix_log(LOG_LEVEL_WARNING,
					"Cannot destroy embedded scripting engine environment: %s", errmsg);
			trx_free(errmsg);
		}
	}

	alerter_send_result(socket, output, ret, error);

	trx_free(output);
	trx_free(error);
	trx_free(params);
	trx_free(script_bin);
}

/******************************************************************************
 *                                                                            *
 * Function: main_alerter_loop                                                *
 *                                                                            *
 * Purpose: periodically check table alerts and send notifications if needed  *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
TRX_THREAD_ENTRY(alerter_thread, args)
{
#define	STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

	char			*error = NULL;
	int			success_num = 0, fail_num = 0;
	trx_ipc_socket_t	alerter_socket;
	trx_ipc_message_t	message;
	double			time_stat, time_idle = 0, time_now, time_read;

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	trx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));

	trx_es_init(&es_engine);

	trx_ipc_message_init(&message);

	if (FAIL == trx_ipc_socket_open(&alerter_socket, TRX_IPC_SERVICE_ALERTER, SEC_PER_MIN, &error))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot connect to alert manager service: %s", error);
		trx_free(error);
		exit(EXIT_FAILURE);
	}

	alerter_register(&alerter_socket);

	time_stat = trx_time();

	trx_setproctitle("%s #%d started", get_process_type_string(process_type), process_num);

	update_selfmon_counter(TRX_PROCESS_STATE_BUSY);

	while (TRX_IS_RUNNING())
	{
		time_now = trx_time();

		if (STAT_INTERVAL < time_now - time_stat)
		{
			trx_setproctitle("%s #%d [sent %d, failed %d alerts, idle " TRX_FS_DBL " sec during "
					TRX_FS_DBL " sec]", get_process_type_string(process_type), process_num,
					success_num, fail_num, time_idle, time_now - time_stat);

			time_stat = time_now;
			time_idle = 0;
			success_num = 0;
			fail_num = 0;
		}

		update_selfmon_counter(TRX_PROCESS_STATE_IDLE);

		if (SUCCEED != trx_ipc_socket_read(&alerter_socket, &message))
		{
			treegix_log(LOG_LEVEL_CRIT, "cannot read alert manager service request");
			exit(EXIT_FAILURE);
		}

		update_selfmon_counter(TRX_PROCESS_STATE_BUSY);

		time_read = trx_time();
		time_idle += time_read - time_now;
		trx_update_env(time_read);

		switch (message.code)
		{
			case TRX_IPC_ALERTER_EMAIL:
				alerter_process_email(&alerter_socket, &message);
				break;
			case TRX_IPC_ALERTER_SMS:
				alerter_process_sms(&alerter_socket, &message);
				break;
			case TRX_IPC_ALERTER_EXEC:
				alerter_process_exec(&alerter_socket, &message);
				break;
			case TRX_IPC_ALERTER_WEBHOOK:
				alerter_process_webhook(&alerter_socket, &message);
				break;
		}

		trx_ipc_message_clean(&message);
	}

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);

	trx_ipc_socket_close(&alerter_socket);
}
