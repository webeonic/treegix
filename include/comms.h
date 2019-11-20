
#ifndef TREEGIX_COMMS_H
#define TREEGIX_COMMS_H

#ifdef _WINDOWS
#	if defined(__INT_MAX__) && __INT_MAX__ == 2147483647
typedef int	ssize_t;
#	else
typedef long	ssize_t;
#	endif
#endif

#ifdef _WINDOWS
#	define TRX_TCP_WRITE(s, b, bl)		((ssize_t)send((s), (b), (int)(bl), 0))
#	define TRX_TCP_READ(s, b, bl)		((ssize_t)recv((s), (b), (int)(bl), 0))
#	define trx_socket_close(s)		if (TRX_SOCKET_ERROR != (s)) closesocket(s)
#	define trx_socket_last_error()		WSAGetLastError()
#	define trx_bind(s, a, l)		(bind((s), (a), (int)(l)))
#	define trx_sendto(fd, b, n, f, a, l)	(sendto((fd), (b), (int)(n), (f), (a), (l)))

#	define TRX_PROTO_AGAIN			WSAEINTR
#	define TRX_PROTO_ERROR			SOCKET_ERROR
#	define TRX_SOCKET_ERROR			INVALID_SOCKET
#	define TRX_SOCKET_TO_INT(s)		((int)(s))
#else
#	define TRX_TCP_WRITE(s, b, bl)		((ssize_t)write((s), (b), (bl)))
#	define TRX_TCP_READ(s, b, bl)		((ssize_t)read((s), (b), (bl)))
#	define trx_socket_close(s)		if (TRX_SOCKET_ERROR != (s)) close(s)
#	define trx_socket_last_error()		errno
#	define trx_bind(s, a, l)		(bind((s), (a), (l)))
#	define trx_sendto(fd, b, n, f, a, l)	(sendto((fd), (b), (n), (f), (a), (l)))

#	define TRX_PROTO_AGAIN		EINTR
#	define TRX_PROTO_ERROR		-1
#	define TRX_SOCKET_ERROR		-1
#	define TRX_SOCKET_TO_INT(s)	(s)
#endif

#ifdef _WINDOWS
typedef SOCKET	TRX_SOCKET;
#else
typedef int	TRX_SOCKET;
#endif

#if defined(HAVE_IPV6)
#	define TRX_SOCKADDR struct sockaddr_storage
#else
#	define TRX_SOCKADDR struct sockaddr_in
#endif

typedef enum
{
	TRX_BUF_TYPE_STAT = 0,
	TRX_BUF_TYPE_DYN
}
trx_buf_type_t;

#define TRX_SOCKET_COUNT	256
#define TRX_STAT_BUF_LEN	2048

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
typedef struct trx_tls_context	trx_tls_context_t;
#endif

typedef struct
{
	TRX_SOCKET			socket;
	TRX_SOCKET			socket_orig;
	size_t				read_bytes;
	char				*buffer;
	char				*next_line;
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	trx_tls_context_t		*tls_ctx;
#endif
	unsigned int 			connection_type;	/* type of connection actually established: */
								/* TRX_TCP_SEC_UNENCRYPTED, TRX_TCP_SEC_TLS_PSK or */
								/* TRX_TCP_SEC_TLS_CERT */
	int				timeout;
	trx_buf_type_t			buf_type;
	unsigned char			accepted;
	int				num_socks;
	TRX_SOCKET			sockets[TRX_SOCKET_COUNT];
	char				buf_stat[TRX_STAT_BUF_LEN];
	TRX_SOCKADDR			peer_info;		/* getpeername() result */
	/* Peer host DNS name or IP address for diagnostics (after TCP connection is established). */
	/* TLS connection may be shut down at any time and it will not be possible to get peer IP address anymore. */
	char				peer[MAX_TRX_DNSNAME_LEN + 1];
	int				protocol;
}
trx_socket_t;

const char	*trx_socket_strerror(void);

#ifndef _WINDOWS
void	trx_gethost_by_ip(const char *ip, char *host, size_t hostlen);
void	trx_getip_by_host(const char *host, char *ip, size_t iplen);
#endif

int	trx_tcp_connect(trx_socket_t *s, const char *source_ip, const char *ip, unsigned short port, int timeout,
		unsigned int tls_connect, const char *tls_arg1, const char *tls_arg2);

#define TRX_TCP_PROTOCOL		0x01
#define TRX_TCP_COMPRESS		0x02

#define TRX_TCP_SEC_UNENCRYPTED		1		/* do not use encryption with this socket */
#define TRX_TCP_SEC_TLS_PSK		2		/* use TLS with pre-shared key (PSK) with this socket */
#define TRX_TCP_SEC_TLS_CERT		4		/* use TLS with certificate with this socket */
#define TRX_TCP_SEC_UNENCRYPTED_TXT	"unencrypted"
#define TRX_TCP_SEC_TLS_PSK_TXT		"psk"
#define TRX_TCP_SEC_TLS_CERT_TXT	"cert"

const char	*trx_tcp_connection_type_name(unsigned int type);

#define trx_tcp_send(s, d)				trx_tcp_send_ext((s), (d), strlen(d), TRX_TCP_PROTOCOL, 0)
#define trx_tcp_send_to(s, d, timeout)			trx_tcp_send_ext((s), (d), strlen(d), TRX_TCP_PROTOCOL, timeout)
#define trx_tcp_send_bytes_to(s, d, len, timeout)	trx_tcp_send_ext((s), (d), len, TRX_TCP_PROTOCOL, timeout)
#define trx_tcp_send_raw(s, d)				trx_tcp_send_ext((s), (d), strlen(d), 0, 0)

int	trx_tcp_send_ext(trx_socket_t *s, const char *data, size_t len, unsigned char flags, int timeout);

void	trx_tcp_close(trx_socket_t *s);

#ifdef HAVE_IPV6
int	get_address_family(const char *addr, int *family, char *error, int max_error_len);
#endif

int	trx_tcp_listen(trx_socket_t *s, const char *listen_ip, unsigned short listen_port);

int	trx_tcp_accept(trx_socket_t *s, unsigned int tls_accept);
void	trx_tcp_unaccept(trx_socket_t *s);

#define TRX_TCP_READ_UNTIL_CLOSE 0x01

#define	trx_tcp_recv(s)			SUCCEED_OR_FAIL(trx_tcp_recv_ext(s, 0))
#define	trx_tcp_recv_to(s, timeout)	SUCCEED_OR_FAIL(trx_tcp_recv_ext(s, timeout))
#define	trx_tcp_recv_raw(s)		SUCCEED_OR_FAIL(trx_tcp_recv_raw_ext(s, 0))

ssize_t		trx_tcp_recv_ext(trx_socket_t *s, int timeout);
ssize_t		trx_tcp_recv_raw_ext(trx_socket_t *s, int timeout);
const char	*trx_tcp_recv_line(trx_socket_t *s);

int	trx_validate_peer_list(const char *peer_list, char **error);
int	trx_tcp_check_allowed_peers(const trx_socket_t *s, const char *peer_list);

int	trx_udp_connect(trx_socket_t *s, const char *source_ip, const char *ip, unsigned short port, int timeout);
int	trx_udp_send(trx_socket_t *s, const char *data, size_t data_len, int timeout);
int	trx_udp_recv(trx_socket_t *s, int timeout);
void	trx_udp_close(trx_socket_t *s);

#define TRX_DEFAULT_FTP_PORT		21
#define TRX_DEFAULT_SSH_PORT		22
#define TRX_DEFAULT_TELNET_PORT		23
#define TRX_DEFAULT_SMTP_PORT		25
#define TRX_DEFAULT_DNS_PORT		53
#define TRX_DEFAULT_HTTP_PORT		80
#define TRX_DEFAULT_POP_PORT		110
#define TRX_DEFAULT_NNTP_PORT		119
#define TRX_DEFAULT_NTP_PORT		123
#define TRX_DEFAULT_IMAP_PORT		143
#define TRX_DEFAULT_LDAP_PORT		389
#define TRX_DEFAULT_HTTPS_PORT		443
#define TRX_DEFAULT_AGENT_PORT		10050
#define TRX_DEFAULT_SERVER_PORT		10051
#define TRX_DEFAULT_GATEWAY_PORT	10052

#define TRX_DEFAULT_AGENT_PORT_STR	"10050"
#define TRX_DEFAULT_SERVER_PORT_STR	"10051"

int	trx_send_response_ext(trx_socket_t *sock, int result, const char *info, const char *version, int protocol,
		int timeout);

#define trx_send_response(sock, result, info, timeout) \
		trx_send_response_ext(sock, result, info, NULL, TRX_TCP_PROTOCOL, timeout)

#define trx_send_proxy_response(sock, result, info, timeout) \
		trx_send_response_ext(sock, result, info, TREEGIX_VERSION, TRX_TCP_PROTOCOL | TRX_TCP_COMPRESS, timeout)

int	trx_recv_response(trx_socket_t *sock, int timeout, char **error);

#ifdef HAVE_IPV6
#	define trx_getnameinfo(sa, host, hostlen, serv, servlen, flags)		\
			getnameinfo(sa, AF_INET == (sa)->sa_family ?		\
					sizeof(struct sockaddr_in) :		\
					sizeof(struct sockaddr_in6),		\
					host, hostlen, serv, servlen, flags)
#endif

#ifdef _WINDOWS
int	trx_socket_start(char **error);
#endif

#endif
