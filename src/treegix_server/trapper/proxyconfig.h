

#ifndef TREEGIX_PROXYCFG_H
#define TREEGIX_PROXYCFG_H

#include "comms.h"
#include "trxjson.h"

extern int	CONFIG_TIMEOUT;
extern int	CONFIG_TRAPPER_TIMEOUT;

void	send_proxyconfig(trx_socket_t *sock, struct trx_json_parse *jp);
void	recv_proxyconfig(trx_socket_t *sock, struct trx_json_parse *jp);

#endif
