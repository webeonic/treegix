

#ifndef TREEGIX_CHECKS_DB_H
#define TREEGIX_CHECKS_DB_H

#include "common.h"

#include "dbcache.h"
#include "sysinfo.h"

#ifdef HAVE_UNIXODBC
int	get_value_db(DC_ITEM *item, AGENT_RESULT *result);
#endif

#endif
