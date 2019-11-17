

#ifndef TREEGIX_CHECKS_TELNET_H
#define TREEGIX_CHECKS_TELNET_H

#include "common.h"
#include "dbcache.h"
#include "sysinfo.h"

extern char	*CONFIG_SOURCE_IP;

int	get_value_telnet(DC_ITEM *item, AGENT_RESULT *result);

#endif
