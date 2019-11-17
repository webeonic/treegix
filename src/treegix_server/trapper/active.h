

#ifndef TREEGIX_TRAPPER_ACTIVE_H
#define TREEGIX_TRAPPER_ACTIVE_H

#include "common.h"
#include "db.h"
#include "comms.h"
#include "zbxjson.h"

extern int	CONFIG_TIMEOUT;

int	send_list_of_active_checks(zbx_socket_t *sock, char *request);
int	send_list_of_active_checks_json(zbx_socket_t *sock, struct zbx_json_parse *json);

#endif
