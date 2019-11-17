

#ifndef TREEGIX_PROXYDATA_H
#define TREEGIX_PROXYDATA_H

#include "comms.h"
#include "zbxjson.h"

extern int	CONFIG_TIMEOUT;
extern int	CONFIG_TRAPPER_TIMEOUT;

void	zbx_recv_proxy_data(zbx_socket_t *sock, struct zbx_json_parse *jp, zbx_timespec_t *ts);
void	zbx_send_proxy_data(zbx_socket_t *sock, zbx_timespec_t *ts);
void	zbx_send_task_data(zbx_socket_t *sock, zbx_timespec_t *ts);

int	zbx_send_proxy_data_response(const DC_PROXY *proxy, zbx_socket_t *sock, const char *info);

int	init_proxy_history_lock(char **error);
void	free_proxy_history_lock(void);

#endif
