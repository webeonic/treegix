

#ifndef TREEGIX_TLS_TCP_ACTIVE_H
#define TREEGIX_TLS_TCP_ACTIVE_H

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
typedef struct
{
	const char	*psk_identity;
	size_t		psk_identity_len;
	char		issuer[HOST_TLS_ISSUER_LEN_MAX];
	char		subject[HOST_TLS_SUBJECT_LEN_MAX];
}
zbx_tls_conn_attr_t;

int		zbx_tls_get_attr_cert(const zbx_socket_t *s, zbx_tls_conn_attr_t *attr);
int		zbx_tls_get_attr_psk(const zbx_socket_t *s, zbx_tls_conn_attr_t *attr);
int		zbx_check_server_issuer_subject(zbx_socket_t *sock, char **error);
unsigned int	zbx_tls_get_psk_usage(void);
#endif

#endif	/* TREEGIX_TLS_TCP_ACTIVE_H */
