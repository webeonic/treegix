

#ifndef TREEGIX_CHECKS_SSH_H
#define TREEGIX_CHECKS_SSH_H

#include "common.h"

#ifdef HAVE_SSH2
#include "dbcache.h"
#include "sysinfo.h"

extern char	*CONFIG_SOURCE_IP;
extern char	*CONFIG_SSH_KEY_LOCATION;

int	get_value_ssh(DC_ITEM *item, AGENT_RESULT *result);
#endif	/* HAVE_SSH2 */

#endif
