

#ifndef TREEGIX_CHECKS_INTERNAL_H
#define TREEGIX_CHECKS_INTERNAL_H

#include "common.h"
#include "dbcache.h"
#include "sysinfo.h"
#include "preproc.h"

extern int	CONFIG_SERVER_STARTUP_TIME;

int	get_value_internal(DC_ITEM *item, AGENT_RESULT *result);

int	zbx_get_value_internal_ext(const char *query, const AGENT_REQUEST *request, AGENT_RESULT *result);

#endif
