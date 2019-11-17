

#ifndef TREEGIX_SYSINFO_COMMON_FILE_H
#define TREEGIX_SYSINFO_COMMON_FILE_H

#include "sysinfo.h"

#define MAX_FILE_LEN (1024 * 1024)

int	VFS_FILE_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result);
int	VFS_FILE_TIME(AGENT_REQUEST *request, AGENT_RESULT *result);
int	VFS_FILE_EXISTS(AGENT_REQUEST *request, AGENT_RESULT *result);
int	VFS_FILE_CONTENTS(AGENT_REQUEST *request, AGENT_RESULT *result);
int	VFS_FILE_REGEXP(AGENT_REQUEST *request, AGENT_RESULT *result);
int	VFS_FILE_REGMATCH(AGENT_REQUEST *request, AGENT_RESULT *result);
int	VFS_FILE_MD5SUM(AGENT_REQUEST *request, AGENT_RESULT *result);
int	VFS_FILE_CKSUM(AGENT_REQUEST *request, AGENT_RESULT *result);

#endif /* TREEGIX_SYSINFO_COMMON_FILE_H */
