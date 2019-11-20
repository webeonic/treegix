

#include "common.h"

#ifdef HAVE_OPENIPMI

#include "log.h"
#include "trxserialize.h"
#include "dbcache.h"

#include "trxipcservice.h"
#include "ipmi_protocol.h"
#include "checks_ipmi.h"
#include "trxserver.h"
#include "ipmi.h"

/******************************************************************************
 *                                                                            *
 * Function: trx_ipmi_port_expand_macros                                      *
 *                                                                            *
 * Purpose: expands user macros in IPMI port value and converts the result to *
 *          to unsigned short value                                           *
 *                                                                            *
 * Parameters: hostid    - [IN] the host identifier                           *
 *             port_orig - [IN] the original port value                       *
 *             port      - [OUT] the resulting port value                     *
 *             error     - [OUT] the error message                            *
 *                                                                            *
 * Return value: SUCCEED - the value was converted successfully               *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
int	trx_ipmi_port_expand_macros(trx_uint64_t hostid, const char *port_orig, unsigned short *port, char **error)
{
	char	*tmp;
	int	ret = SUCCEED;

	tmp = trx_strdup(NULL, port_orig);
	substitute_simple_macros(NULL, NULL, NULL, NULL, &hostid, NULL, NULL, NULL, NULL,
			&tmp, MACRO_TYPE_COMMON, NULL, 0);

	if (FAIL == is_ushort(tmp, port) || 0 == *port)
	{
		*error = trx_dsprintf(*error, "Invalid port value \"%s\"", port_orig);
		ret = FAIL;
	}

	trx_free(tmp);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_ipmi_execute_command                                         *
 *                                                                            *
 * Purpose: executes IPMI command                                             *
 *                                                                            *
 * Parameters: host          - [IN] the target host                           *
 *             command       - [IN] the command to execute                    *
 *             error         - [OUT] the error message buffer                 *
 *             max_error_len - [IN] the size of error message buffer          *
 *                                                                            *
 * Return value: SUCCEED - the command was executed successfully              *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
int	trx_ipmi_execute_command(const DC_HOST *host, const char *command, char *error, size_t max_error_len)
{
	trx_ipc_socket_t	ipmi_socket;
	trx_ipc_message_t	message;
	char			*errmsg = NULL, sensor[ITEM_IPMI_SENSOR_LEN_MAX], *value = NULL;
	trx_uint32_t		data_len;
	unsigned char		*data = NULL;
	int			ret = FAIL, op;
	DC_INTERFACE		interface;
	trx_timespec_t		ts;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() host:\"%s\" command:%s", __func__, host->host, command);

	if (SUCCEED != trx_parse_ipmi_command(command, sensor, &op, error, max_error_len))
		goto out;

	if (FAIL == trx_ipc_socket_open(&ipmi_socket, TRX_IPC_SERVICE_IPMI, SEC_PER_MIN, &errmsg))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot connect to IPMI service: %s", errmsg);
		exit(EXIT_FAILURE);
	}

	trx_ipc_message_init(&message);

	if (FAIL == DCconfig_get_interface_by_type(&interface, host->hostid, INTERFACE_TYPE_IPMI))
	{
		trx_strlcpy(error, "cannot find host IPMI interface", max_error_len);
		goto cleanup;
	}

	if (FAIL == trx_ipmi_port_expand_macros(host->hostid, interface.port_orig, &interface.port, &errmsg))
	{
		trx_strlcpy(error, errmsg, max_error_len);
		trx_free(errmsg);
		goto cleanup;
	}

	data_len = trx_ipmi_serialize_request(&data, host->hostid, interface.addr, interface.port, host->ipmi_authtype,
			host->ipmi_privilege, host->ipmi_username, host->ipmi_password, sensor, op);

	if (FAIL == trx_ipc_socket_write(&ipmi_socket, TRX_IPC_IPMI_SCRIPT_REQUEST, data, data_len))
	{
		trx_strlcpy(error, "cannot send script request message to IPMI service", max_error_len);
		goto cleanup;
	}

	trx_ipc_message_init(&message);

	if (FAIL == trx_ipc_socket_read(&ipmi_socket, &message))
	{
		trx_strlcpy(error,  "cannot read script request response from IPMI service", max_error_len);
		goto cleanup;
	}

	if (TRX_IPC_IPMI_SCRIPT_RESULT != message.code)
	{
		trx_snprintf(error, max_error_len, "invalid response code:%u received from IPMI service", message.code);
		goto cleanup;
	}

	trx_ipmi_deserialize_result(message.data, &ts, &ret, &value);

	if (SUCCEED != ret)
		trx_strlcpy(error, value, max_error_len);
cleanup:
	trx_free(value);
	trx_free(data);
	trx_ipc_message_clean(&message);
	trx_ipc_socket_close(&ipmi_socket);

out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

#endif
