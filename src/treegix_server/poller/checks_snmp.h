

#ifndef TREEGIX_CHECKS_SNMP_H
#define TREEGIX_CHECKS_SNMP_H

#include "common.h"
#include "log.h"
#include "dbcache.h"
#include "sysinfo.h"

extern char	*CONFIG_SOURCE_IP;
extern int	CONFIG_TIMEOUT;

#ifdef HAVE_NETSNMP
void	trx_init_snmp(void);
int	get_value_snmp(const DC_ITEM *item, AGENT_RESULT *result);
void	get_values_snmp(const DC_ITEM *items, AGENT_RESULT *results, int *errcodes, int num);
#endif

#endif
