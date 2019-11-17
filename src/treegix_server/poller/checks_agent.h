

#ifndef TREEGIX_CHECKS_AGENT_H
#define TREEGIX_CHECKS_AGENT_H

#include "dbcache.h"
#include "sysinfo.h"

extern char	*CONFIG_SOURCE_IP;

int	get_value_agent(DC_ITEM *item, AGENT_RESULT *result);

#endif
