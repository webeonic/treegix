

#ifndef TREEGIX_TLS_H
#define TREEGIX_TLS_H

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)

#if defined(_WINDOWS)
/* Typical thread is long-running, if necessary, it initializes TLS for itself. Treegix sender is an exception. If */
/* data is sent from a file or in real time then sender's 'main' thread starts the 'send_value' thread for each   */
/* 250 values to be sent. To avoid TLS initialization on every start of 'send_value' thread we initialize TLS in  */
/* 'main' thread and use this structure for passing minimum TLS variables into 'send_value' thread. */

#if defined(HAVE_POLARSSL)
#	include <polarssl/entropy.h>
#	include <polarssl/ctr_drbg.h>
#	include <polarssl/ssl.h>
#elif defined(HAVE_GNUTLS)
#	include <gnutls/gnutls.h>
#elif defined(HAVE_OPENSSL)
#	include <openssl/ssl.h>
#endif

typedef struct
{
#if defined(HAVE_POLARSSL)
	char			*my_psk;
	size_t			my_psk_len;
	char			*my_psk_identity;
	size_t			my_psk_identity_len;
	x509_crt		*ca_cert;
	x509_crl		*crl;
	x509_crt		*my_cert;
	pk_context		*my_priv_key;
	entropy_context		*entropy;
	ctr_drbg_context	*ctr_drbg;
	int			*ciphersuites_cert;
	int			*ciphersuites_psk;
#elif defined(HAVE_GNUTLS)
	gnutls_certificate_credentials_t	my_cert_creds;
	gnutls_psk_client_credentials_t		my_psk_client_creds;
	gnutls_priority_t			ciphersuites_cert;
	gnutls_priority_t			ciphersuites_psk;
#elif defined(HAVE_OPENSSL)
	SSL_CTX			*ctx_cert;
#ifdef HAVE_OPENSSL_WITH_PSK
	SSL_CTX			*ctx_psk;
	const char		*psk_identity_for_cb;
	size_t			psk_identity_len_for_cb;
	char			*psk_for_cb;
	size_t			psk_len_for_cb;
#endif
#endif
}
TRX_THREAD_SENDVAL_TLS_ARGS;

void	trx_tls_pass_vars(TRX_THREAD_SENDVAL_TLS_ARGS *args);
void	trx_tls_take_vars(TRX_THREAD_SENDVAL_TLS_ARGS *args);
#endif	/* #if defined(_WINDOWS) */

void	trx_tls_validate_config(void);
void	trx_tls_library_deinit(void);
void	trx_tls_init_parent(void);
void	trx_tls_init_child(void);
void	trx_tls_free(void);
void	trx_tls_free_on_signal(void);
void	trx_tls_version(void);

#endif	/* #if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL) */

#endif	/* TREEGIX_TLS_H */
