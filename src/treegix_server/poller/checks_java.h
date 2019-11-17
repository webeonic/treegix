

#ifndef TREEGIX_CHECKS_JAVA_H
#define TREEGIX_CHECKS_JAVA_H

#include "dbcache.h"
#include "sysinfo.h"

#define TRX_JAVA_GATEWAY_REQUEST_INTERNAL	0
#define TRX_JAVA_GATEWAY_REQUEST_JMX		1

extern char	*CONFIG_SOURCE_IP;
extern char	*CONFIG_JAVA_GATEWAY;
extern int	CONFIG_JAVA_GATEWAY_PORT;

int	get_value_java(unsigned char request, const DC_ITEM *item, AGENT_RESULT *result);
void	get_values_java(unsigned char request, const DC_ITEM *items, AGENT_RESULT *results, int *errcodes, int num);

#endif
