

#ifndef TREEGIX_DISCOVERY_H
#define TREEGIX_DISCOVERY_H

#include "comms.h"

typedef struct
{
	zbx_uint64_t	dcheckid;
	unsigned short	port;
	char		dns[INTERFACE_DNS_LEN_MAX];
	char		value[MAX_DISCOVERED_VALUE_SIZE];
	int		status;
	time_t		itemtime;
}
zbx_service_t;

void	discovery_update_host(DB_DHOST *dhost, int status, int now);
void	discovery_update_service(const DB_DRULE *drule, zbx_uint64_t dcheckid, DB_DHOST *dhost,
		const char *ip, const char *dns, int port, int status, const char *value, int now);
#endif
