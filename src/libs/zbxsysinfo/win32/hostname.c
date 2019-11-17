

#include "sysinfo.h"
#include "log.h"

TRX_METRIC	parameter_hostname =
/*	KEY			FLAG		FUNCTION		TEST PARAMETERS */
	{"system.hostname",     CF_HAVEPARAMS,  SYSTEM_HOSTNAME,        NULL};

int	SYSTEM_HOSTNAME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	DWORD	dwSize = 256;
	wchar_t	computerName[256];
	char	*type, buffer[256];
	int	netbios;

	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	type = get_rparam(request, 0);

	if (NULL == type || '\0' == *type || 0 == strcmp(type, "netbios"))
		netbios = 1;
	else if (0 == strcmp(type, "host"))
		netbios = 0;
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (1 == netbios)
	{
		/* Buffer size is chosen large enough to contain any DNS name, not just MAX_COMPUTERNAME_LENGTH + 1 */
		/* characters. MAX_COMPUTERNAME_LENGTH is usually less than 32, but it varies among systems, so we  */
		/* cannot use the constant in a precompiled Windows agent, which is expected to work on any system. */
		if (0 == GetComputerName(computerName, &dwSize))
		{
			treegix_log(LOG_LEVEL_ERR, "GetComputerName() failed: %s", strerror_from_system(GetLastError()));
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain computer name: %s",
					strerror_from_system(GetLastError())));
			return SYSINFO_RET_FAIL;
		}

		SET_STR_RESULT(result, zbx_unicode_to_utf8(computerName));
	}
	else
	{
		if (SUCCEED != gethostname(buffer, sizeof(buffer)))
		{
			treegix_log(LOG_LEVEL_ERR, "gethostname() failed: %s", strerror_from_system(WSAGetLastError()));
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain host name: %s",
					strerror_from_system(WSAGetLastError())));
			return SYSINFO_RET_FAIL;
		}

		SET_STR_RESULT(result, zbx_strdup(NULL, buffer));
	}

	return SYSINFO_RET_OK;
}
