

#ifndef TREEGIX_SYSINFO_COMMON_H
#define TREEGIX_SYSINFO_COMMON_H

#include "sysinfo.h"

extern TRX_METRIC	parameters_common[];

int	EXECUTE_USER_PARAMETER(AGENT_REQUEST *request, AGENT_RESULT *result);
int	EXECUTE_STR(const char *command, AGENT_RESULT *result);
int	EXECUTE_DBL(const char *command, AGENT_RESULT *result);
int	EXECUTE_INT(const char *command, AGENT_RESULT *result);

#endif
