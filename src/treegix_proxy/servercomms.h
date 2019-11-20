

#ifndef TREEGIX_SERVERCOMMS_H
#define TREEGIX_SERVERCOMMS_H

extern char	*CONFIG_SOURCE_IP;
extern char	*CONFIG_SERVER;
extern int	CONFIG_SERVER_PORT;
extern char	*CONFIG_HOSTNAME;

#include "comms.h"

int	connect_to_server(trx_socket_t *sock, int timeout, int retry_interval);
void	disconnect_server(trx_socket_t *sock);

int	get_data_from_server(trx_socket_t *sock, const char *request, char **error);
int	put_data_to_server(trx_socket_t *sock, struct trx_json *j, char **error);

#endif
