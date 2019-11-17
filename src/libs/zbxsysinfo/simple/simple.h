

#ifndef TREEGIX_SYSINFO_SIMPLE_H
#define TREEGIX_SYSINFO_SIMPLE_H

#include "sysinfo.h"

extern char		*CONFIG_SOURCE_IP;
extern ZBX_METRIC	parameters_simple[];

int	check_service(AGENT_REQUEST *request, const char *default_addr, AGENT_RESULT *result, int perf);

int	CHECK_SERVICE_PERF(AGENT_REQUEST *request, AGENT_RESULT *result);
int	CHECK_SERVICE(AGENT_REQUEST *request, AGENT_RESULT *result);

#endif /* TREEGIX_SYSINFO_SIMPLE_H */
