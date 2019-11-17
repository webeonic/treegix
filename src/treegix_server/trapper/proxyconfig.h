

#ifndef TREEGIX_PROXYCFG_H
#define TREEGIX_PROXYCFG_H

#include "comms.h"
#include "zbxjson.h"

extern int	CONFIG_TIMEOUT;
extern int	CONFIG_TRAPPER_TIMEOUT;

void	send_proxyconfig(zbx_socket_t *sock, struct zbx_json_parse *jp);
void	recv_proxyconfig(zbx_socket_t *sock, struct zbx_json_parse *jp);

#endif
