
#ifndef TREEGIX_TELNET_H
#define TREEGIX_TELNET_H

#include "sysinfo.h"
#include "comms.h"

#define WAIT_READ	0
#define WAIT_WRITE	1

#define CMD_IAC		255
#define CMD_WILL	251
#define CMD_WONT	252
#define CMD_DO		253
#define CMD_DONT	254
#define OPT_SGA		3

int	telnet_test_login(ZBX_SOCKET socket_fd);
int	telnet_login(ZBX_SOCKET socket_fd, const char *username, const char *password, AGENT_RESULT *result);
int	telnet_execute(ZBX_SOCKET socket_fd, const char *command, AGENT_RESULT *result, const char *encoding);

#endif
