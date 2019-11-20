

#ifndef TREEGIX_TLS_TCP_H
#define TREEGIX_TLS_TCP_H

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
int	trx_tls_connect(trx_socket_t *s, unsigned int tls_connect, const char *tls_arg1, const char *tls_arg2,
		char **error);
int	trx_tls_accept(trx_socket_t *s, unsigned int tls_accept, char **error);
ssize_t	trx_tls_write(trx_socket_t *s, const char *buf, size_t len, char **error);
ssize_t	trx_tls_read(trx_socket_t *s, char *buf, size_t len, char **error);
void	trx_tls_close(trx_socket_t *s);
#endif

#if defined(HAVE_OPENSSL)
void	trx_tls_error_msg(char **error, size_t *error_alloc, size_t *error_offset);
#endif

#endif	/* TREEGIX_TLS_TCP_H */
