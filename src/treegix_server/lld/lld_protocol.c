

#include "common.h"
#include "log.h"
#include "trxserialize.h"
#include "trxipcservice.h"
#include "lld_protocol.h"
#include "sysinfo.h"
#include "trxlld.h"

/******************************************************************************
 *                                                                            *
 * Function: trx_lld_serialize_item_value                                     *
 *                                                                            *
 ******************************************************************************/
trx_uint32_t	trx_lld_serialize_item_value(unsigned char **data, trx_uint64_t itemid, const char *value,
		const trx_timespec_t *ts, unsigned char meta, trx_uint64_t lastlogsize, int mtime, const char *error)
{
	unsigned char	*ptr;
	trx_uint32_t	data_len = 0, value_len, error_len;

	trx_serialize_prepare_value(data_len, itemid);
	trx_serialize_prepare_str(data_len, value);
	trx_serialize_prepare_value(data_len, *ts);
	trx_serialize_prepare_str(data_len, error);

	trx_serialize_prepare_value(data_len, meta);
	if (0 != meta)
	{
		trx_serialize_prepare_value(data_len, lastlogsize);
		trx_serialize_prepare_value(data_len, mtime);
	}

	*data = (unsigned char *)trx_malloc(NULL, data_len);

	ptr = *data;
	ptr += trx_serialize_value(ptr, itemid);
	ptr += trx_serialize_str(ptr, value, value_len);
	ptr += trx_serialize_value(ptr, *ts);
	ptr += trx_serialize_str(ptr, error, error_len);
	ptr += trx_serialize_value(ptr, meta);
	if (0 != meta)
	{
		ptr += trx_serialize_value(ptr, lastlogsize);
		(void)trx_serialize_value(ptr, mtime);
	}

	return data_len;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_lld_deserialize_item_value                                   *
 *                                                                            *
 ******************************************************************************/
void	trx_lld_deserialize_item_value(const unsigned char *data, trx_uint64_t *itemid, char **value,
		trx_timespec_t *ts, unsigned char *meta, trx_uint64_t *lastlogsize, int *mtime, char **error)
{
	trx_uint32_t	value_len, error_len;

	data += trx_deserialize_value(data, itemid);
	data += trx_deserialize_str(data, value, value_len);
	data += trx_deserialize_value(data, ts);
	data += trx_deserialize_str(data, error, error_len);
	data += trx_deserialize_value(data, meta);
	if (0 != *meta)
	{
		data += trx_deserialize_value(data, lastlogsize);
		(void)trx_deserialize_value(data, mtime);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: trx_lld_process_value                                            *
 *                                                                            *
 * Purpose: process low level discovery value/error                           *
 *                                                                            *
 * Parameters: itemid - [IN] the LLD rule id                                  *
 *             value  - [IN] the rule value (can be NULL if error is set)     *
 *             ts     - [IN] the value timestamp                              *
 *             error  - [IN] the error message (can be NULL)                  *
 *                                                                            *
 ******************************************************************************/
void	trx_lld_process_value(trx_uint64_t itemid, const char *value, const trx_timespec_t *ts, unsigned char meta,
		trx_uint64_t lastlogsize, int mtime, const char *error)
{
	static trx_ipc_socket_t	socket;
	char			*errmsg = NULL;
	unsigned char		*data;
	trx_uint32_t		data_len;

	/* each process has a permanent connection to manager */
	if (0 == socket.fd && FAIL == trx_ipc_socket_open(&socket, TRX_IPC_SERVICE_LLD, SEC_PER_MIN, &errmsg))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot connect to LLD manager service: %s", errmsg);
		exit(EXIT_FAILURE);
	}

	data_len = trx_lld_serialize_item_value(&data, itemid, value, ts, meta, lastlogsize, mtime, error);

	if (FAIL == trx_ipc_socket_write(&socket, TRX_IPC_LLD_REQUEST, data, data_len))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot send data to LLD manager service");
		exit(EXIT_FAILURE);
	}

	trx_free(data);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_lld_process_agent_result                                     *
 *                                                                            *
 * Purpose: process low level discovery agent result                          *
 *                                                                            *
 * Parameters: itemid - [IN] the LLD rule id                                  *
 *             result - [IN] the agent result                                 *
 *             ts     - [IN] the value timestamp                              *
 *             error  - [IN] the error message (can be NULL)                  *
 *                                                                            *
 ******************************************************************************/
void	trx_lld_process_agent_result(trx_uint64_t itemid, AGENT_RESULT *result, trx_timespec_t *ts, char *error)
{
	const char	*value = NULL;
	unsigned char	meta = 0;
	trx_uint64_t	lastlogsize = 0;
	int		mtime = 0;

	if (NULL != result)
	{
		if (NULL != GET_TEXT_RESULT(result))
			value = *(GET_TEXT_RESULT(result));

		if (0 != ISSET_META(result))
		{
			meta = 1;
			lastlogsize = result->lastlogsize;
			mtime = result->mtime;
		}
	}

	if (NULL != value || NULL != error || 0 != meta)
		trx_lld_process_value(itemid, value, ts, meta, lastlogsize, mtime, error);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_lld_get_queue_size                                           *
 *                                                                            *
 * Purpose: get queue size (enqueued value count) of LLD manager              *
 *                                                                            *
 * Parameters: size  - [OUT] the queue size                                   *
 *             error - [OUT] the error message                                *
 *                                                                            *
 * Return value: SUCCEED - the queue size was returned successfully           *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
int	trx_lld_get_queue_size(trx_uint64_t *size, char **error)
{
	trx_ipc_message_t	message;
	trx_ipc_socket_t	lld_socket;
	int			ret = FAIL;

	if (FAIL == trx_ipc_socket_open(&lld_socket, TRX_IPC_SERVICE_LLD, SEC_PER_MIN, error))
		return FAIL;

	trx_ipc_message_init(&message);

	if (FAIL == trx_ipc_socket_write(&lld_socket, TRX_IPC_LLD_QUEUE, NULL, 0))
	{
		*error = trx_strdup(NULL, "cannot send queue request to LLD manager service");
		goto out;
	}

	if (FAIL == trx_ipc_socket_read(&lld_socket, &message))
	{
		*error = trx_strdup(NULL, "cannot read queue response from LLD manager service");
		goto out;
	}

	memcpy(size, message.data, sizeof(trx_uint64_t));
	ret = SUCCEED;
out:
	trx_ipc_socket_close(&lld_socket);
	trx_ipc_message_clean(&message);

	return ret;
}
