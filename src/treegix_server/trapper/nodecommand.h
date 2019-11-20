


#ifndef TREEGIX_NODECOMMAND_H
#define TREEGIX_NODECOMMAND_H

#include "comms.h"
#include "trxjson.h"

extern int	CONFIG_TIMEOUT;
extern int	CONFIG_TRAPPER_TIMEOUT;
extern char	*CONFIG_SOURCE_IP;

int	node_process_command(trx_socket_t *sock, const char *data, struct trx_json_parse *jp);

#endif
