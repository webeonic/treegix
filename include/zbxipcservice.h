

#ifndef TREEGIX_TRXIPCSERVICE_H
#define TREEGIX_TRXIPCSERVICE_H

#include "common.h"
#include "zbxalgo.h"

#define TRX_IPC_SOCKET_BUFFER_SIZE	4096

#define TRX_IPC_RECV_IMMEDIATE	0
#define TRX_IPC_RECV_WAIT	1
#define TRX_IPC_RECV_TIMEOUT	2

#define TRX_IPC_WAIT_FOREVER	-1

typedef struct
{
	/* the message code */
	zbx_uint32_t	code;

	/* the data size */
	zbx_uint32_t	size;

	/* the data */
	unsigned char	*data;
}
zbx_ipc_message_t;

/* Messaging socket, providing blocking connections to IPC service. */
/* The IPC socket api is used for simple write/read operations.     */
typedef struct
{
	/* socket descriptor */
	int		fd;

	/* incoming data buffer */
	unsigned char	rx_buffer[TRX_IPC_SOCKET_BUFFER_SIZE];
	zbx_uint32_t	rx_buffer_bytes;
	zbx_uint32_t	rx_buffer_offset;
}
zbx_ipc_socket_t;

typedef struct zbx_ipc_client zbx_ipc_client_t;

/* IPC service */
typedef struct
{
	/* the listening socket descriptor */
	int			fd;

	struct event_base	*ev;
	struct event		*ev_listener;
	struct event		*ev_timer;

	/* the unix socket path */
	char			*path;

	/* the connected clients */
	zbx_vector_ptr_t	clients;

	/* the clients with messages */
	zbx_queue_ptr_t		clients_recv;
}
zbx_ipc_service_t;

typedef struct
{
	zbx_ipc_client_t	*client;

	struct event_base	*ev;
	struct event		*ev_timer;

	unsigned char		state;
}
zbx_ipc_async_socket_t;

int	zbx_ipc_service_init_env(const char *path, char **error);
void	zbx_ipc_service_free_env(void);
int	zbx_ipc_service_start(zbx_ipc_service_t *service, const char *service_name, char **error);
int	zbx_ipc_service_recv(zbx_ipc_service_t *service, int timeout, zbx_ipc_client_t **client,
		zbx_ipc_message_t **message);
void	zbx_ipc_service_close(zbx_ipc_service_t *service);

int	zbx_ipc_client_send(zbx_ipc_client_t *client, zbx_uint32_t code, const unsigned char *data, zbx_uint32_t size);
void	zbx_ipc_client_close(zbx_ipc_client_t *client);

void			zbx_ipc_client_addref(zbx_ipc_client_t *client);
void			zbx_ipc_client_release(zbx_ipc_client_t *client);
int			zbx_ipc_client_connected(zbx_ipc_client_t *client);
zbx_uint64_t		zbx_ipc_client_id(const zbx_ipc_client_t *client);
zbx_ipc_client_t	*zbx_ipc_client_by_id(const zbx_ipc_service_t *service, zbx_uint64_t id);

int	zbx_ipc_socket_open(zbx_ipc_socket_t *csocket, const char *service_name, int timeout, char **error);
void	zbx_ipc_socket_close(zbx_ipc_socket_t *csocket);
int	zbx_ipc_socket_write(zbx_ipc_socket_t *csocket, zbx_uint32_t code, const unsigned char *data,
		zbx_uint32_t size);
int	zbx_ipc_socket_read(zbx_ipc_socket_t *csocket, zbx_ipc_message_t *message);

int	zbx_ipc_async_socket_open(zbx_ipc_async_socket_t *asocket, const char *service_name, int timeout, char **error);
void	zbx_ipc_async_socket_close(zbx_ipc_async_socket_t *asocket);
int	zbx_ipc_async_socket_send(zbx_ipc_async_socket_t *asocket, zbx_uint32_t code, const unsigned char *data,
		zbx_uint32_t size);
int	zbx_ipc_async_socket_recv(zbx_ipc_async_socket_t *asocket, int timeout, zbx_ipc_message_t **message);
int	zbx_ipc_async_socket_flush(zbx_ipc_async_socket_t *asocket, int timeout);
int	zbx_ipc_async_socket_check_unsent(zbx_ipc_async_socket_t *asocket);
int	zbx_ipc_async_exchange(const char *service_name, zbx_uint32_t code, int timeout, const unsigned char *data,
		zbx_uint32_t size, unsigned char **out, char **error);


void	zbx_ipc_message_free(zbx_ipc_message_t *message);
void	zbx_ipc_message_clean(zbx_ipc_message_t *message);
void	zbx_ipc_message_init(zbx_ipc_message_t *message);
void	zbx_ipc_message_format(const zbx_ipc_message_t *message, char **data);
void	zbx_ipc_message_copy(zbx_ipc_message_t *dst, const zbx_ipc_message_t *src);

#endif

