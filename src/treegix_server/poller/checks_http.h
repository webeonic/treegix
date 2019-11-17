

#ifndef TREEGIX_CHECKS_HTTP_H
#define TREEGIX_CHECKS_HTTP_H

#include "common.h"

#ifdef HAVE_LIBCURL
#include "dbcache.h"

int	get_value_http(const DC_ITEM *item, AGENT_RESULT *result);
#endif

#endif
