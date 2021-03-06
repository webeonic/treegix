

#include "common.h"
#include "../poller/checks_agent.h"
#include "../ipmi/ipmi.h"
#include "../poller/checks_ssh.h"
#include "../poller/checks_telnet.h"
#include "trxexec.h"
#include "trxserver.h"
#include "db.h"
#include "log.h"
#include "trxtasks.h"
#include "scripts.h"

extern int	CONFIG_TRAPPER_TIMEOUT;

static int	trx_execute_script_on_agent(const DC_HOST *host, const char *command, char **result,
		char *error, size_t max_error_len)
{
	int		ret;
	AGENT_RESULT	agent_result;
	char		*param = NULL, *port = NULL;
	DC_ITEM		item;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	*error = '\0';
	memset(&item, 0, sizeof(item));
	memcpy(&item.host, host, sizeof(item.host));

	if (SUCCEED != (ret = DCconfig_get_interface_by_type(&item.interface, host->hostid, INTERFACE_TYPE_AGENT)))
	{
		trx_snprintf(error, max_error_len, "Treegix agent interface is not defined for host [%s]", host->host);
		goto fail;
	}

	port = trx_strdup(port, item.interface.port_orig);
	substitute_simple_macros(NULL, NULL, NULL, NULL, &host->hostid, NULL, NULL, NULL, NULL,
			&port, MACRO_TYPE_COMMON, NULL, 0);

	if (SUCCEED != (ret = is_ushort(port, &item.interface.port)))
	{
		trx_snprintf(error, max_error_len, "Invalid port number [%s]", item.interface.port_orig);
		goto fail;
	}

	param = trx_strdup(param, command);
	if (SUCCEED != (ret = quote_key_param(&param, 0)))
	{
		trx_snprintf(error, max_error_len, "Invalid param [%s]", param);
		goto fail;
	}

	item.key = trx_dsprintf(item.key, "system.run[%s,%s]", param, NULL == result ? "nowait" : "wait");
	item.value_type = ITEM_VALUE_TYPE_TEXT;

	init_result(&agent_result);

	trx_alarm_on(CONFIG_TIMEOUT);

	if (SUCCEED != (ret = get_value_agent(&item, &agent_result)))
	{
		if (ISSET_MSG(&agent_result))
			trx_strlcpy(error, agent_result.msg, max_error_len);
		ret = FAIL;
	}
	else if (NULL != result && ISSET_TEXT(&agent_result))
		*result = trx_strdup(*result, agent_result.text);

	trx_alarm_off();

	free_result(&agent_result);

	trx_free(item.key);
fail:
	trx_free(port);
	trx_free(param);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

static int	trx_execute_script_on_terminal(const DC_HOST *host, const trx_script_t *script, char **result,
		char *error, size_t max_error_len)
{
	int		ret = FAIL, i;
	AGENT_RESULT	agent_result;
	DC_ITEM		item;
	int             (*function)(DC_ITEM *, AGENT_RESULT *);

#ifdef HAVE_SSH2
	assert(TRX_SCRIPT_TYPE_SSH == script->type || TRX_SCRIPT_TYPE_TELNET == script->type);
#else
	assert(TRX_SCRIPT_TYPE_TELNET == script->type);
#endif

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	*error = '\0';
	memset(&item, 0, sizeof(item));
	memcpy(&item.host, host, sizeof(item.host));

	for (i = 0; INTERFACE_TYPE_COUNT > i; i++)
	{
		if (SUCCEED == (ret = DCconfig_get_interface_by_type(&item.interface, host->hostid,
				INTERFACE_TYPE_PRIORITY[i])))
		{
			break;
		}
	}

	if (FAIL == ret)
	{
		trx_snprintf(error, max_error_len, "No interface defined for host [%s]", host->host);
		goto fail;
	}

	switch (script->type)
	{
		case TRX_SCRIPT_TYPE_SSH:
			item.authtype = script->authtype;
			item.publickey = script->publickey;
			item.privatekey = script->privatekey;
			TRX_FALLTHROUGH;
		case TRX_SCRIPT_TYPE_TELNET:
			item.username = script->username;
			item.password = script->password;
			break;
	}

#ifdef HAVE_SSH2
	if (TRX_SCRIPT_TYPE_SSH == script->type)
	{
		item.key = trx_dsprintf(item.key, "ssh.run[,,%s]", script->port);
		function = get_value_ssh;
	}
	else
	{
#endif
		item.key = trx_dsprintf(item.key, "telnet.run[,,%s]", script->port);
		function = get_value_telnet;
#ifdef HAVE_SSH2
	}
#endif
	item.value_type = ITEM_VALUE_TYPE_TEXT;
	item.params = trx_strdup(item.params, script->command);

	init_result(&agent_result);

	trx_alarm_on(CONFIG_TIMEOUT);

	if (SUCCEED != (ret = function(&item, &agent_result)))
	{
		if (ISSET_MSG(&agent_result))
			trx_strlcpy(error, agent_result.msg, max_error_len);
		ret = FAIL;
	}
	else if (NULL != result && ISSET_TEXT(&agent_result))
		*result = trx_strdup(*result, agent_result.text);

	trx_alarm_off();

	free_result(&agent_result);

	trx_free(item.params);
	trx_free(item.key);
fail:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

static int	DBget_script_by_scriptid(trx_uint64_t scriptid, trx_script_t *script, trx_uint64_t *groupid)
{
	DB_RESULT	result;
	DB_ROW		row;
	int		ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	result = DBselect(
			"select type,execute_on,command,groupid,host_access"
			" from scripts"
			" where scriptid=" TRX_FS_UI64,
			scriptid);

	if (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UCHAR(script->type, row[0]);
		TRX_STR2UCHAR(script->execute_on, row[1]);
		script->command = trx_strdup(script->command, row[2]);
		TRX_DBROW2UINT64(*groupid, row[3]);
		TRX_STR2UCHAR(script->host_access, row[4]);
		ret = SUCCEED;
	}
	DBfree_result(result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

static int	check_script_permissions(trx_uint64_t groupid, trx_uint64_t hostid)
{
	DB_RESULT		result;
	int			ret = SUCCEED;
	trx_vector_uint64_t	groupids;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() groupid:" TRX_FS_UI64 " hostid:" TRX_FS_UI64, __func__, groupid, hostid);

	if (0 == groupid)
		goto exit;

	trx_vector_uint64_create(&groupids);
	trx_dc_get_nested_hostgroupids(&groupid, 1, &groupids);

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select hostid"
			" from hosts_groups"
			" where hostid=" TRX_FS_UI64
				" and",
			hostid);

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "groupid", groupids.values,
			groupids.values_num);

	result = DBselect("%s", sql);

	trx_free(sql);
	trx_vector_uint64_destroy(&groupids);

	if (NULL == DBfetch(result))
		ret = FAIL;

	DBfree_result(result);
exit:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

static int	check_user_permissions(trx_uint64_t userid, const DC_HOST *host, trx_script_t *script)
{
	int		ret = SUCCEED;
	DB_RESULT	result;
	DB_ROW		row;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() userid:" TRX_FS_UI64 " hostid:" TRX_FS_UI64 " scriptid:" TRX_FS_UI64,
			__func__, userid, host->hostid, script->scriptid);

	result = DBselect(
		"select null"
			" from hosts_groups hg,rights r,users_groups ug"
		" where hg.groupid=r.id"
			" and r.groupid=ug.usrgrpid"
			" and hg.hostid=" TRX_FS_UI64
			" and ug.userid=" TRX_FS_UI64
		" group by hg.hostid"
		" having min(r.permission)>%d"
			" and max(r.permission)>=%d",
		host->hostid,
		userid,
		PERM_DENY,
		script->host_access);

	if (NULL == (row = DBfetch(result)))
		ret = FAIL;

	DBfree_result(result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

void	trx_script_init(trx_script_t *script)
{
	memset(script, 0, sizeof(trx_script_t));
}

void	trx_script_clean(trx_script_t *script)
{
	trx_free(script->port);
	trx_free(script->username);
	trx_free(script->publickey);
	trx_free(script->privatekey);
	trx_free(script->password);
	trx_free(script->command);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_script_prepare                                               *
 *                                                                            *
 * Purpose: prepares user script                                              *
 *                                                                            *
 * Parameters: host          - [IN] the host the script will be executed on   *
 *             script        - [IN/OUT] the script to prepare                 *
 *             user          - [IN] the user executing script                 *
 *             error         - [OUT] the error message output buffer          *
 *             mas_error_len - [IN] the size of error message output buffer   *
 *                                                                            *
 * Return value:  SUCCEED - the script has been prepared successfully         *
 *                FAIL    - otherwise, error contains error message           *
 *                                                                            *
 * Comments: This function prepares script for execution by loading global    *
 *           script/expanding macros.                                         *
 *           Prepared scripts must be always freed with trx_script_clean()    *
 *           function.                                                        *
 *                                                                            *
 ******************************************************************************/
int	trx_script_prepare(trx_script_t *script, const DC_HOST *host, const trx_user_t *user, char *error,
		size_t max_error_len)
{
	int		ret = FAIL;
	trx_uint64_t	groupid;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	switch (script->type)
	{
		case TRX_SCRIPT_TYPE_CUSTOM_SCRIPT:
			dos2unix(script->command);	/* CR+LF (Windows) => LF (Unix) */
			break;
		case TRX_SCRIPT_TYPE_SSH:
			substitute_simple_macros(NULL, NULL, NULL, NULL, &host->hostid, NULL, NULL, NULL, NULL,
					&script->publickey, MACRO_TYPE_COMMON, NULL, 0);
			substitute_simple_macros(NULL, NULL, NULL, NULL, &host->hostid, NULL, NULL, NULL, NULL,
					&script->privatekey, MACRO_TYPE_COMMON, NULL, 0);
			TRX_FALLTHROUGH;
		case TRX_SCRIPT_TYPE_TELNET:
			substitute_simple_macros(NULL, NULL, NULL, NULL, &host->hostid, NULL, NULL, NULL, NULL,
					&script->port, MACRO_TYPE_COMMON, NULL, 0);

			if ('\0' != *script->port && SUCCEED != (ret = is_ushort(script->port, NULL)))
			{
				trx_snprintf(error, max_error_len, "Invalid port number \"%s\"", script->port);
				goto out;
			}

			substitute_simple_macros(NULL, NULL, NULL, NULL, &host->hostid, NULL, NULL, NULL, NULL,
					&script->username, MACRO_TYPE_COMMON, NULL, 0);
			substitute_simple_macros(NULL, NULL, NULL, NULL, &host->hostid, NULL, NULL, NULL, NULL,
					&script->password, MACRO_TYPE_COMMON, NULL, 0);
			break;
		case TRX_SCRIPT_TYPE_GLOBAL_SCRIPT:
			if (SUCCEED != DBget_script_by_scriptid(script->scriptid, script, &groupid))
			{
				trx_strlcpy(error, "Unknown script identifier.", max_error_len);
				goto out;
			}
			if (groupid > 0 && SUCCEED != check_script_permissions(groupid, host->hostid))
			{
				trx_strlcpy(error, "Script does not have permission to be executed on the host.",
						max_error_len);
				goto out;
			}
			if (user != NULL && USER_TYPE_SUPER_ADMIN != user->type &&
				SUCCEED != check_user_permissions(user->userid, host, script))
			{
				trx_strlcpy(error, "User does not have permission to execute this script on the host.",
						max_error_len);
				goto out;
			}

			if (SUCCEED != substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, host, NULL, NULL,
					NULL, &script->command, MACRO_TYPE_SCRIPT, error, max_error_len))
			{
				goto out;
			}

			/* DBget_script_by_scriptid() may overwrite script type with anything but global script... */
			if (TRX_SCRIPT_TYPE_GLOBAL_SCRIPT == script->type)
			{
				THIS_SHOULD_NEVER_HAPPEN;
				goto out;
			}

			/* ...therefore this recursion is no more than two layers deep */
			if (FAIL == trx_script_prepare(script, host, user, error, max_error_len))
				goto out;

			break;
		case TRX_SCRIPT_TYPE_IPMI:
			break;
		default:
			trx_snprintf(error, max_error_len, "Invalid command type \"%d\".", (int)script->type);
			goto out;
	}

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_script_execute                                               *
 *                                                                            *
 * Purpose: executing user scripts or remote commands                         *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                TIMEOUT_ERROR - a timeout occurred                          *
 *                                                                            *
 ******************************************************************************/
int	trx_script_execute(const trx_script_t *script, const DC_HOST *host, char **result, char *error,
		size_t max_error_len)
{
	int	ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	*error = '\0';

	switch (script->type)
	{
		case TRX_SCRIPT_TYPE_CUSTOM_SCRIPT:
			switch (script->execute_on)
			{
				case TRX_SCRIPT_EXECUTE_ON_AGENT:
					ret = trx_execute_script_on_agent(host, script->command, result, error,
							max_error_len);
					break;
				case TRX_SCRIPT_EXECUTE_ON_SERVER:
				case TRX_SCRIPT_EXECUTE_ON_PROXY:
					ret = trx_execute(script->command, result, error, max_error_len,
							CONFIG_TRAPPER_TIMEOUT, TRX_EXIT_CODE_CHECKS_ENABLED);
					break;
				default:
					trx_snprintf(error, max_error_len, "Invalid 'Execute on' option \"%d\".",
							(int)script->execute_on);
			}
			break;
		case TRX_SCRIPT_TYPE_IPMI:
#ifdef HAVE_OPENIPMI
			if (SUCCEED == (ret = trx_ipmi_execute_command(host, script->command, error, max_error_len)))
			{
				if (NULL != result)
					*result = trx_strdup(*result, "IPMI command successfully executed.");
			}
#else
			trx_strlcpy(error, "Support for IPMI commands was not compiled in.", max_error_len);
#endif
			break;
		case TRX_SCRIPT_TYPE_SSH:
#ifndef HAVE_SSH2
			trx_strlcpy(error, "Support for SSH script was not compiled in.", max_error_len);
			break;
#endif
		case TRX_SCRIPT_TYPE_TELNET:
			ret = trx_execute_script_on_terminal(host, script, result, error, max_error_len);
			break;
		default:
			trx_snprintf(error, max_error_len, "Invalid command type \"%d\".", (int)script->type);
	}

	if (SUCCEED != ret && NULL != result)
		*result = trx_strdup(*result, "");

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_script_create_task                                           *
 *                                                                            *
 * Purpose: creates remote command task from a script                         *
 *                                                                            *
 * Return value:  the identifier of the created task or 0 in the case of      *
 *                error                                                       *
 *                                                                            *
 ******************************************************************************/
trx_uint64_t	trx_script_create_task(const trx_script_t *script, const DC_HOST *host, trx_uint64_t alertid, int now)
{
	trx_tm_task_t	*task;
	unsigned short	port;
	trx_uint64_t	taskid;

	if (NULL != script->port && '\0' != script->port[0])
		is_ushort(script->port, &port);
	else
		port = 0;

	taskid = DBget_maxid("task");

	task = trx_tm_task_create(taskid, TRX_TM_TASK_REMOTE_COMMAND, TRX_TM_STATUS_NEW, now,
			TRX_REMOTE_COMMAND_TTL, host->proxy_hostid);

	task->data = trx_tm_remote_command_create(script->type, script->command, script->execute_on, port,
			script->authtype, script->username, script->password, script->publickey, script->privatekey,
			taskid, host->hostid, alertid);

	DBbegin();

	if (FAIL == trx_tm_save_task(task))
		taskid = 0;

	DBcommit();

	trx_tm_task_free(task);

	return taskid;
}
