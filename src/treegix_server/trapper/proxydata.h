

#ifndef TREEGIX_PROXYDATA_H
#define TREEGIX_PROXYDATA_H

#include "comms.h"
#include "trxjson.h"

extern int	CONFIG_TIMEOUT;
extern int	CONFIG_TRAPPER_TIMEOUT;

void	trx_recv_proxy_data(trx_socket_t *sock, struct trx_json_parse *jp, trx_timespec_t *ts);
void	trx_send_proxy_data(trx_socket_t *sock, trx_timespec_t *ts);
void	trx_send_task_data(trx_socket_t *sock, trx_timespec_t *ts);

int	trx_send_proxy_data_response(const DC_PROXY *proxy, trx_socket_t *sock, const char *info);

int	init_proxy_history_lock(char **error);
void	free_proxy_history_lock(void);

#endif
