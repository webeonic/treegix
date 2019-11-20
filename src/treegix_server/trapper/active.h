

#ifndef TREEGIX_TRAPPER_ACTIVE_H
#define TREEGIX_TRAPPER_ACTIVE_H

#include "common.h"
#include "db.h"
#include "comms.h"
#include "trxjson.h"

extern int	CONFIG_TIMEOUT;

int	send_list_of_active_checks(trx_socket_t *sock, char *request);
int	send_list_of_active_checks_json(trx_socket_t *sock, struct trx_json_parse *json);

#endif
