

#include "common.h"
#include "nodecommand.h"
#include "comms.h"
#include "trxserver.h"
#include "db.h"
#include "log.h"
#include "../scripts/scripts.h"


/******************************************************************************
 *                                                                            *
 * Function: execute_remote_script                                            *
 *                                                                            *
 * Purpose: execute remote command and wait for the result                    *
 *                                                                            *
 * Return value:  SUCCEED - the remote command was executed successfully      *
 *                FAIL    - an error occurred                                 *
 *                                                                            *
 ******************************************************************************/
static int	execute_remote_script(trx_script_t *script, DC_HOST *host, char **info, char *error,
		size_t max_error_len)
{
	int		ret = FAIL, time_start;
	trx_uint64_t	taskid;
	DB_RESULT	result = NULL;
	DB_ROW		row;

	if (0 == (taskid = trx_script_create_task(script, host, 0, time(NULL))))
	{
		trx_snprintf(error, max_error_len, "Cannot create remote command task.");
		return FAIL;
	}

	for (time_start = time(NULL); SEC_PER_MIN > time(NULL) - time_start; sleep(1))
	{
		result = DBselect(
				"select tr.status,tr.info"
				" from task t"
				" left join task_remote_command_result tr"
					" on tr.taskid=t.taskid"
				" where tr.parent_taskid=" TRX_FS_UI64,
				taskid);

		if (NULL != (row = DBfetch(result)))
		{
			if (SUCCEED == (ret = atoi(row[0])))
				*info = trx_strdup(*info, row[1]);
			else
				trx_strlcpy(error, row[1], max_error_len);

			DBfree_result(result);
			return ret;
		}

		DBfree_result(result);
	}

	trx_snprintf(error, max_error_len, "Timeout while waiting for remote command result.");

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: execute_script                                                   *
 *                                                                            *
 * Purpose: executing command                                                 *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
static int	execute_script(trx_uint64_t scriptid, trx_uint64_t hostid, const char *sessionid, char **result)
{
	char		error[MAX_STRING_LEN];
	int		ret = FAIL, rc;
	DC_HOST		host;
	trx_script_t	script;
	trx_user_t	user;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() scriptid:" TRX_FS_UI64 " hostid:" TRX_FS_UI64 " sessionid:%s",
			__func__, scriptid, hostid, sessionid);

	*error = '\0';

	if (SUCCEED != (rc = DCget_host_by_hostid(&host, hostid)))
	{
		trx_strlcpy(error, "Unknown host identifier.", sizeof(error));
		goto fail;
	}

	if (SUCCEED != (rc = DBget_user_by_active_session(sessionid, &user)))
	{
		trx_strlcpy(error, "Permission denied.", sizeof(error));
		goto fail;
	}

	trx_script_init(&script);

	script.type = TRX_SCRIPT_TYPE_GLOBAL_SCRIPT;
	script.scriptid = scriptid;

	if (SUCCEED == (ret = trx_script_prepare(&script, &host, &user, error, sizeof(error))))
	{
		if (0 == host.proxy_hostid || TRX_SCRIPT_EXECUTE_ON_SERVER == script.execute_on)
			ret = trx_script_execute(&script, &host, result, error, sizeof(error));
		else
			ret = execute_remote_script(&script, &host, result, error, sizeof(error));
	}

	trx_script_clean(&script);
fail:
	if (SUCCEED != ret)
		*result = trx_strdup(*result, error);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: node_process_command                                             *
 *                                                                            *
 * Purpose: process command received from the frontend                        *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
int	node_process_command(trx_socket_t *sock, const char *data, struct trx_json_parse *jp)
{
	char		*result = NULL, *send = NULL, tmp[64], sessionid[MAX_STRING_LEN];
	int		ret = FAIL;
	trx_uint64_t	scriptid, hostid;
	struct trx_json	j;

	treegix_log(LOG_LEVEL_DEBUG, "In node_process_command()");

	trx_json_init(&j, TRX_JSON_STAT_BUF_LEN);

	if (SUCCEED != trx_json_value_by_name(jp, TRX_PROTO_TAG_SCRIPTID, tmp, sizeof(tmp)) ||
			FAIL == is_uint64(tmp, &scriptid))
	{
		result = trx_dsprintf(result, "Failed to parse command request tag: %s.", TRX_PROTO_TAG_SCRIPTID);
		goto finish;
	}

	if (SUCCEED != trx_json_value_by_name(jp, TRX_PROTO_TAG_HOSTID, tmp, sizeof(tmp)) ||
			FAIL == is_uint64(tmp, &hostid))
	{
		result = trx_dsprintf(result, "Failed to parse command request tag: %s.", TRX_PROTO_TAG_HOSTID);
		goto finish;
	}

	if (SUCCEED != trx_json_value_by_name(jp, TRX_PROTO_TAG_SID, sessionid, sizeof(sessionid)))
	{
		result = trx_dsprintf(result, "Failed to parse command request tag: %s.", TRX_PROTO_TAG_SID);
		goto finish;
	}

	if (SUCCEED == (ret = execute_script(scriptid, hostid, sessionid, &result)))
	{
		trx_json_addstring(&j, TRX_PROTO_TAG_RESPONSE, TRX_PROTO_VALUE_SUCCESS, TRX_JSON_TYPE_STRING);
		trx_json_addstring(&j, TRX_PROTO_TAG_DATA, result, TRX_JSON_TYPE_STRING);
		send = j.buffer;
	}
finish:
	if (SUCCEED != ret)
	{
		trx_json_addstring(&j, TRX_PROTO_TAG_RESPONSE, TRX_PROTO_VALUE_FAILED, TRX_JSON_TYPE_STRING);
		trx_json_addstring(&j, TRX_PROTO_TAG_INFO, (NULL != result ? result : "Unknown error."),
				TRX_JSON_TYPE_STRING);
		send = j.buffer;
	}

	trx_alarm_on(CONFIG_TIMEOUT);
	if (SUCCEED != trx_tcp_send(sock, send))
		treegix_log(LOG_LEVEL_WARNING, "Error sending result of command");
	else
		treegix_log(LOG_LEVEL_DEBUG, "Sending back command '%s' result '%s'", data, send);
	trx_alarm_off();

	trx_json_free(&j);
	trx_free(result);

	return ret;
}
