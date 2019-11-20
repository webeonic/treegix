

#ifndef TREEGIX_TRXIPCSERVICE_H
#define TREEGIX_TRXIPCSERVICE_H

#include "common.h"
#include "trxalgo.h"

#define TRX_IPC_SOCKET_BUFFER_SIZE	4096

#define TRX_IPC_RECV_IMMEDIATE	0
#define TRX_IPC_RECV_WAIT	1
#define TRX_IPC_RECV_TIMEOUT	2

#define TRX_IPC_WAIT_FOREVER	-1

typedef struct
{
	/* the message code */
	trx_uint32_t	code;

	/* the data size */
	trx_uint32_t	size;

	/* the data */
	unsigned char	*data;
}
trx_ipc_message_t;

/* Messaging socket, providing blocking connections to IPC service. */
/* The IPC socket api is used for simple write/read operations.     */
typedef struct
{
	/* socket descriptor */
	int		fd;

	/* incoming data buffer */
	unsigned char	rx_buffer[TRX_IPC_SOCKET_BUFFER_SIZE];
	trx_uint32_t	rx_buffer_bytes;
	trx_uint32_t	rx_buffer_offset;
}
trx_ipc_socket_t;

typedef struct trx_ipc_client trx_ipc_client_t;

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
	trx_vector_ptr_t	clients;

	/* the clients with messages */
	trx_queue_ptr_t		clients_recv;
}
trx_ipc_service_t;

typedef struct
{
	trx_ipc_client_t	*client;

	struct event_base	*ev;
	struct event		*ev_timer;

	unsigned char		state;
}
trx_ipc_async_socket_t;

int	trx_ipc_service_init_env(const char *path, char **error);
void	trx_ipc_service_free_env(void);
int	trx_ipc_service_start(trx_ipc_service_t *service, const char *service_name, char **error);
int	trx_ipc_service_recv(trx_ipc_service_t *service, int timeout, trx_ipc_client_t **client,
		trx_ipc_message_t **message);
void	trx_ipc_service_close(trx_ipc_service_t *service);

int	trx_ipc_client_send(trx_ipc_client_t *client, trx_uint32_t code, const unsigned char *data, trx_uint32_t size);
void	trx_ipc_client_close(trx_ipc_client_t *client);

void			trx_ipc_client_addref(trx_ipc_client_t *client);
void			trx_ipc_client_release(trx_ipc_client_t *client);
int			trx_ipc_client_connected(trx_ipc_client_t *client);
trx_uint64_t		trx_ipc_client_id(const trx_ipc_client_t *client);
trx_ipc_client_t	*trx_ipc_client_by_id(const trx_ipc_service_t *service, trx_uint64_t id);

int	trx_ipc_socket_open(trx_ipc_socket_t *csocket, const char *service_name, int timeout, char **error);
void	trx_ipc_socket_close(trx_ipc_socket_t *csocket);
int	trx_ipc_socket_write(trx_ipc_socket_t *csocket, trx_uint32_t code, const unsigned char *data,
		trx_uint32_t size);
int	trx_ipc_socket_read(trx_ipc_socket_t *csocket, trx_ipc_message_t *message);

int	trx_ipc_async_socket_open(trx_ipc_async_socket_t *asocket, const char *service_name, int timeout, char **error);
void	trx_ipc_async_socket_close(trx_ipc_async_socket_t *asocket);
int	trx_ipc_async_socket_send(trx_ipc_async_socket_t *asocket, trx_uint32_t code, const unsigned char *data,
		trx_uint32_t size);
int	trx_ipc_async_socket_recv(trx_ipc_async_socket_t *asocket, int timeout, trx_ipc_message_t **message);
int	trx_ipc_async_socket_flush(trx_ipc_async_socket_t *asocket, int timeout);
int	trx_ipc_async_socket_check_unsent(trx_ipc_async_socket_t *asocket);
int	trx_ipc_async_exchange(const char *service_name, trx_uint32_t code, int timeout, const unsigned char *data,
		trx_uint32_t size, unsigned char **out, char **error);


void	trx_ipc_message_free(trx_ipc_message_t *message);
void	trx_ipc_message_clean(trx_ipc_message_t *message);
void	trx_ipc_message_init(trx_ipc_message_t *message);
void	trx_ipc_message_format(const trx_ipc_message_t *message, char **data);
void	trx_ipc_message_copy(trx_ipc_message_t *dst, const trx_ipc_message_t *src);

#endif

