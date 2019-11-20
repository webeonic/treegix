

#include "common.h"

#ifdef HAVE_OPENIPMI

#include "dbcache.h"
#include "daemon.h"
#include "trxself.h"
#include "log.h"
#include "trxipcservice.h"

#include "ipmi_manager.h"
#include "ipmi_protocol.h"
#include "checks_ipmi.h"
#include "ipmi_poller.h"

#define TRX_IPMI_MANAGER_CLEANUP_DELAY		SEC_PER_DAY

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

/******************************************************************************
 *                                                                            *
 * Function: ipmi_poller_register                                             *
 *                                                                            *
 * Purpose: registers IPMI poller with IPMI manager                           *
 *                                                                            *
 * Parameters: socket - [IN] the connections socket                           *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_poller_register(trx_ipc_async_socket_t *socket)
{
	pid_t	ppid;

	ppid = getppid();

	trx_ipc_async_socket_send(socket, TRX_IPC_IPMI_REGISTER, (unsigned char *)&ppid, sizeof(ppid));
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_poller_send_result                                          *
 *                                                                            *
 * Purpose: sends IPMI poll result to manager                                 *
 *                                                                            *
 * Parameters: socket  - [IN] the connections socket                          *
 *             itemid  - [IN] the item identifier                             *
 *             errcode - [IN] the result error code                           *
 *             value   - [IN] the resulting value/error message               *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_poller_send_result(trx_ipc_async_socket_t *socket, trx_uint32_t code, int errcode,
		const char *value)
{
	unsigned char	*data;
	trx_uint32_t	data_len;
	trx_timespec_t	ts;

	trx_timespec(&ts);
	data_len = trx_ipmi_serialize_result(&data, &ts, errcode, value);
	trx_ipc_async_socket_send(socket, code, data, data_len);

	trx_free(data);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_poller_process_value_request                                *
 *                                                                            *
 * Purpose: gets IPMI sensor value from the specified host                    *
 *                                                                            *
 * Parameters: socket  - [IN] the connections socket                          *
 *             message - [IN] the value request message                       *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_poller_process_value_request(trx_ipc_async_socket_t *socket, trx_ipc_message_t *message)
{
	trx_uint64_t	itemid;
	char		*addr, *username, *password, *sensor, *value = NULL;
	signed char	authtype;
	unsigned char	privilege;
	unsigned short	port;
	int		errcode, command;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_ipmi_deserialize_request(message->data, &itemid, &addr, &port, &authtype,
			&privilege, &username, &password, &sensor, &command);

	treegix_log(LOG_LEVEL_TRACE, "%s() itemid:" TRX_FS_UI64 " addr:%s port:%d authtype:%d privilege:%d username:%s"
			" sensor:%s", __func__, itemid, addr, (int)port, (int)authtype, (int)privilege,
			username, sensor);

	errcode = get_value_ipmi(itemid, addr, port, authtype, privilege, username, password, sensor, &value);
	ipmi_poller_send_result(socket, TRX_IPC_IPMI_VALUE_RESULT, errcode, value);

	trx_free(value);
	trx_free(addr);
	trx_free(username);
	trx_free(password);
	trx_free(sensor);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_poller_process_command_request                              *
 *                                                                            *
 * Purpose:sets IPMI sensor value                                             *
 *                                                                            *
 * Parameters: socket  - [IN] the connections socket                          *
 *             message - [IN] the command request message                     *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_poller_process_command_request(trx_ipc_async_socket_t *socket, trx_ipc_message_t *message)
{
	trx_uint64_t	itemid;
	char		*addr, *username, *password, *sensor, *error = NULL;
	signed char	authtype;
	unsigned char	privilege;
	unsigned short	port;
	int		errcode, command;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_ipmi_deserialize_request(message->data, &itemid, &addr, &port, &authtype,
			&privilege, &username, &password, &sensor, &command);

	treegix_log(LOG_LEVEL_TRACE, "%s() hostid:" TRX_FS_UI64 " addr:%s port:%d authtype:%d privilege:%d username:%s"
			" sensor:%s", __func__, itemid, addr, (int)port, (int)authtype, (int)privilege,
			username, sensor);

	errcode = trx_set_ipmi_control_value(itemid, addr, port, authtype, privilege, username, password, sensor,
			command, &error);

	ipmi_poller_send_result(socket, TRX_IPC_IPMI_COMMAND_RESULT, errcode, error);

	trx_free(error);
	trx_free(addr);
	trx_free(username);
	trx_free(password);
	trx_free(sensor);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

TRX_THREAD_ENTRY(ipmi_poller_thread, args)
{
	char			*error = NULL;
	trx_ipc_async_socket_t	ipmi_socket;
	int			polled_num = 0;
	double			time_stat, time_idle = 0, time_now, time_read;

#define	STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

	process_type = ((trx_thread_args_t *)args)->process_type;

	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	trx_setproctitle("%s #%d starting", get_process_type_string(process_type), process_num);

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	if (FAIL == trx_ipc_async_socket_open(&ipmi_socket, TRX_IPC_SERVICE_IPMI, SEC_PER_MIN, &error))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot connect to IPMI service: %s", error);
		trx_free(error);
		exit(EXIT_FAILURE);
	}

	trx_init_ipmi_handler();

	ipmi_poller_register(&ipmi_socket);

	time_stat = trx_time();

	trx_setproctitle("%s #%d started", get_process_type_string(process_type), process_num);

	update_selfmon_counter(TRX_PROCESS_STATE_BUSY);

	while (TRX_IS_RUNNING())
	{
		trx_ipc_message_t	*message = NULL;

		time_now = trx_time();

		if (STAT_INTERVAL < time_now - time_stat)
		{
			trx_setproctitle("%s #%d [polled %d values, idle " TRX_FS_DBL " sec during "
					TRX_FS_DBL " sec]", get_process_type_string(process_type), process_num,
					polled_num, time_idle, time_now - time_stat);

			time_stat = time_now;
			time_idle = 0;
			polled_num = 0;
		}

		update_selfmon_counter(TRX_PROCESS_STATE_IDLE);

		while (TRX_IS_RUNNING())
		{
			const int ipc_timeout = 2;
			const int ipmi_timeout = 1;

			if (SUCCEED != trx_ipc_async_socket_recv(&ipmi_socket, ipc_timeout, &message))
			{
				treegix_log(LOG_LEVEL_CRIT, "cannot read IPMI service request");
				exit(EXIT_FAILURE);
			}

			if (NULL != message)
				break;

			trx_perform_all_openipmi_ops(ipmi_timeout);
		}

		update_selfmon_counter(TRX_PROCESS_STATE_BUSY);

		if (NULL == message)
			break;

		time_read = trx_time();
		time_idle += time_read - time_now;
		trx_update_env(time_read);

		switch (message->code)
		{
			case TRX_IPC_IPMI_VALUE_REQUEST:
				ipmi_poller_process_value_request(&ipmi_socket, message);
				polled_num++;
				break;
			case TRX_IPC_IPMI_COMMAND_REQUEST:
				ipmi_poller_process_command_request(&ipmi_socket, message);
				break;
			case TRX_IPC_IPMI_CLEANUP_REQUEST:
				trx_delete_inactive_ipmi_hosts(time(NULL));
				break;
		}

		trx_ipc_message_free(message);
		message = NULL;
	}

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);

	trx_ipc_async_socket_close(&ipmi_socket);

	trx_free_ipmi_handler();
#undef STAT_INTERVAL
}

#endif
