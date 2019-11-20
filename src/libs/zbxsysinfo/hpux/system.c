

#include "sysinfo.h"
#include "log.h"

#ifdef HAVE_SYS_UTSNAME_H
#	include <sys/utsname.h>
#endif

int	SYSTEM_UNAME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	struct utsname	name;

	if (-1 == uname(&name))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot obtain system information: %s", trx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	SET_STR_RESULT(result, trx_dsprintf(NULL, "%s %s %s %s %s %s", name.sysname, name.nodename, name.release,
			name.version, name.machine, name.idnumber));

	return SYSINFO_RET_OK;
}
