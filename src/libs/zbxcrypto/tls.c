

#include "common.h"

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)

#include "comms.h"
#include "threads.h"
#include "log.h"
#include "tls.h"
#include "tls_tcp.h"
#include "tls_tcp_active.h"

#if defined(HAVE_POLARSSL)
#	include <polarssl/entropy.h>
#	include <polarssl/ctr_drbg.h>
#	include <polarssl/ssl.h>
#	include <polarssl/error.h>
#	include <polarssl/debug.h>
#	include <polarssl/oid.h>
#	include <polarssl/version.h>
#elif defined(HAVE_GNUTLS)
#	include <gnutls/gnutls.h>
#	include <gnutls/x509.h>
#elif defined(HAVE_OPENSSL)
#	include <openssl/ssl.h>
#	include <openssl/err.h>
#	include <openssl/rand.h>
#endif

/* use only TLS 1.2 (which has number 3.3) with PolarSSL */
#if defined(HAVE_POLARSSL)
#	define TRX_TLS_MIN_MAJOR_VER	SSL_MAJOR_VERSION_3
#	define TRX_TLS_MIN_MINOR_VER	SSL_MINOR_VERSION_3
#	define TRX_TLS_MAX_MAJOR_VER	SSL_MAJOR_VERSION_3
#	define TRX_TLS_MAX_MINOR_VER	SSL_MINOR_VERSION_3
#	define TRX_TLS_CIPHERSUITE_CERT	0			/* select only certificate ciphersuites */
#	define TRX_TLS_CIPHERSUITE_PSK	1			/* select only pre-shared key ciphersuites */
#	define TRX_TLS_CIPHERSUITE_ALL	2			/* select ciphersuites with certificate and PSK */
#endif

#if defined(HAVE_OPENSSL) && OPENSSL_VERSION_NUMBER < 0x1010000fL || defined(LIBRESSL_VERSION_NUMBER)
/* for OpenSSL 1.0.1/1.0.2 (before 1.1.0) or LibreSSL */

/* mutexes for multi-threaded OpenSSL (see "man 3ssl threads" and example in crypto/threads/mttest.c) */

#ifdef _WINDOWS
#include "mutexs.h"

static trx_mutex_t	*crypto_mutexes = NULL;

static void	trx_openssl_locking_cb(int mode, int n, const char *file, int line)
{
	if (0 != (mode & CRYPTO_LOCK))
		__trx_mutex_lock(file, line, *(crypto_mutexes + n));
	else
		__trx_mutex_unlock(file, line, *(crypto_mutexes + n));
}

static void	trx_openssl_thread_setup(void)
{
	int	i, num_locks;

	num_locks = CRYPTO_num_locks();

	if (NULL == (crypto_mutexes = trx_malloc(crypto_mutexes, num_locks * sizeof(trx_mutex_t))))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot allocate mutexes for OpenSSL library");
		exit(EXIT_FAILURE);
	}

	treegix_log(LOG_LEVEL_DEBUG, "%s() creating %d mutexes", __func__, num_locks);

	for (i = 0; i < num_locks; i++)
	{
		char	*error = NULL;

		if (SUCCEED != trx_mutex_create(crypto_mutexes + i, NULL, &error))
		{
			treegix_log(LOG_LEVEL_CRIT, "cannot create mutex #%d for OpenSSL library: %s", i, error);
			trx_free(error);
			exit(EXIT_FAILURE);
		}
	}

	CRYPTO_set_locking_callback((void (*)(int, int, const char *, int))trx_openssl_locking_cb);

	/* do not register our own threadid_func() callback, use OpenSSL default one */
}

static void	trx_openssl_thread_cleanup(void)
{
	int	i, num_locks;

	CRYPTO_set_locking_callback(NULL);

	num_locks = CRYPTO_num_locks();

	for (i = 0; i < num_locks; i++)
		trx_mutex_destroy(crypto_mutexes + i);

	trx_free(crypto_mutexes);
}
#endif	/* _WINDOWS */

#if !defined(LIBRESSL_VERSION_NUMBER)
#define OPENSSL_INIT_LOAD_SSL_STRINGS			0
#define OPENSSL_INIT_LOAD_CRYPTO_STRINGS		0
#define OPENSSL_VERSION					SSLEAY_VERSION
#endif
#define OpenSSL_version					SSLeay_version
#define TLS_method					TLSv1_2_method
#define TLS_client_method				TLSv1_2_client_method
#define SSL_CTX_get_ciphers(ciphers)			((ciphers)->cipher_list)
#if !defined(LIBRESSL_VERSION_NUMBER)
#define SSL_CTX_set_min_proto_version(ctx, TLSv)	1
#endif

static int	trx_openssl_init_ssl(int opts, void *settings)
{
#if defined(HAVE_OPENSSL) && OPENSSL_VERSION_NUMBER < 0x1010000fL
	TRX_UNUSED(opts);
	TRX_UNUSED(settings);

	SSL_load_error_strings();
	ERR_load_BIO_strings();
	SSL_library_init();
#elif defined(LIBRESSL_VERSION_NUMBER)
	OPENSSL_init_ssl(opts, settings);
#endif
#ifdef _WINDOWS
	TRX_UNUSED(opts);
	TRX_UNUSED(settings);
	trx_openssl_thread_setup();
#endif
	return 1;
}

static void	OPENSSL_cleanup(void)
{
	RAND_cleanup();
	ERR_free_strings();
#ifdef _WINDOWS
	trx_openssl_thread_cleanup();
#endif
}
#endif	/* defined(HAVE_OPENSSL) && OPENSSL_VERSION_NUMBER < 0x1010000fL || defined(LIBRESSL_VERSION_NUMBER) */

#if defined(HAVE_OPENSSL) && OPENSSL_VERSION_NUMBER >= 0x1010000fL && !defined(LIBRESSL_VERSION_NUMBER)
/* OpenSSL 1.1.0 or newer, not LibreSSL */
static int	trx_openssl_init_ssl(int opts, void *settings)
{
	return OPENSSL_init_ssl(opts, settings);
}
#endif

struct trx_tls_context
{
#if defined(HAVE_POLARSSL)
	ssl_context			*ctx;
#elif defined(HAVE_GNUTLS)
	gnutls_session_t		ctx;
	gnutls_psk_client_credentials_t	psk_client_creds;
	gnutls_psk_server_credentials_t	psk_server_creds;
#elif defined(HAVE_OPENSSL)
	SSL				*ctx;
#endif
};

extern unsigned int			configured_tls_connect_mode;
extern unsigned int			configured_tls_accept_modes;

extern unsigned char			program_type;

extern int				CONFIG_PASSIVE_FORKS;
extern int				CONFIG_ACTIVE_FORKS;

extern char				*CONFIG_TLS_CONNECT;
extern char				*CONFIG_TLS_ACCEPT;
extern char				*CONFIG_TLS_CA_FILE;
extern char				*CONFIG_TLS_CRL_FILE;
extern char				*CONFIG_TLS_SERVER_CERT_ISSUER;
extern char				*CONFIG_TLS_SERVER_CERT_SUBJECT;
extern char				*CONFIG_TLS_CERT_FILE;
extern char				*CONFIG_TLS_KEY_FILE;
extern char				*CONFIG_TLS_PSK_IDENTITY;
extern char				*CONFIG_TLS_PSK_FILE;

static TRX_THREAD_LOCAL char		*my_psk_identity	= NULL;
static TRX_THREAD_LOCAL size_t		my_psk_identity_len	= 0;
static TRX_THREAD_LOCAL char		*my_psk			= NULL;
static TRX_THREAD_LOCAL size_t		my_psk_len		= 0;

/* Pointer to DCget_psk_by_identity() initialized at runtime. This is a workaround for linking. */
/* Server and proxy link with src/libs/trxdbcache/dbconfig.o where DCget_psk_by_identity() resides */
/* but other components (e.g. agent) do not link dbconfig.o. */
size_t	(*find_psk_in_cache)(const unsigned char *, unsigned char *, unsigned int *) = NULL;

/* variable for passing information from callback functions if PSK was found among host PSKs or autoregistration PSK */
static unsigned int	psk_usage;

#if defined(HAVE_POLARSSL)
static TRX_THREAD_LOCAL x509_crt		*ca_cert		= NULL;
static TRX_THREAD_LOCAL x509_crl		*crl			= NULL;
static TRX_THREAD_LOCAL x509_crt		*my_cert		= NULL;
static TRX_THREAD_LOCAL pk_context		*my_priv_key		= NULL;
static TRX_THREAD_LOCAL entropy_context		*entropy		= NULL;
static TRX_THREAD_LOCAL ctr_drbg_context	*ctr_drbg		= NULL;
static TRX_THREAD_LOCAL char			*err_msg		= NULL;
static TRX_THREAD_LOCAL int			*ciphersuites_cert	= NULL;
static TRX_THREAD_LOCAL int			*ciphersuites_psk	= NULL;
static TRX_THREAD_LOCAL int			*ciphersuites_all	= NULL;
#elif defined(HAVE_GNUTLS)
static TRX_THREAD_LOCAL gnutls_certificate_credentials_t	my_cert_creds		= NULL;
static TRX_THREAD_LOCAL gnutls_psk_client_credentials_t		my_psk_client_creds	= NULL;
static TRX_THREAD_LOCAL gnutls_psk_server_credentials_t		my_psk_server_creds	= NULL;
static TRX_THREAD_LOCAL gnutls_priority_t			ciphersuites_cert	= NULL;
static TRX_THREAD_LOCAL gnutls_priority_t			ciphersuites_psk	= NULL;
static TRX_THREAD_LOCAL gnutls_priority_t			ciphersuites_all	= NULL;
static int							init_done 		= 0;
#elif defined(HAVE_OPENSSL)
static TRX_THREAD_LOCAL const SSL_METHOD	*method			= NULL;
static TRX_THREAD_LOCAL SSL_CTX			*ctx_cert		= NULL;
#ifdef HAVE_OPENSSL_WITH_PSK
static TRX_THREAD_LOCAL SSL_CTX			*ctx_psk		= NULL;
static TRX_THREAD_LOCAL SSL_CTX			*ctx_all		= NULL;
/* variables for passing required PSK identity and PSK info to client callback function */
static TRX_THREAD_LOCAL const char		*psk_identity_for_cb	= NULL;
static TRX_THREAD_LOCAL size_t			psk_identity_len_for_cb	= 0;
static TRX_THREAD_LOCAL char			*psk_for_cb		= NULL;
static TRX_THREAD_LOCAL size_t			psk_len_for_cb		= 0;
#endif
static int					init_done 		= 0;
#ifdef HAVE_OPENSSL_WITH_PSK
/* variables for capturing PSK identity from server callback function */
static TRX_THREAD_LOCAL int			incoming_connection_has_psk = 0;
static TRX_THREAD_LOCAL char			incoming_connection_psk_id[PSK_MAX_IDENTITY_LEN + 1];
#endif
/* buffer for messages produced by trx_openssl_info_cb() */
TRX_THREAD_LOCAL char				info_buf[256];
#endif

#if defined(HAVE_POLARSSL)
/**********************************************************************************
 *                                                                                *
 * Function: trx_make_personalization_string                                      *
 *                                                                                *
 * Purpose: provide additional entropy for initialization of crypto library       *
 *                                                                                *
 * Comments:                                                                      *
 *     For more information about why and how to use personalization strings see  *
 *     https://polarssl.org/module-level-design-rng                               *
 *     http://nvlpubs.nist.gov/nistpubs/SpecialPublications/NIST.SP.800-90Ar1.pdf *
 *                                                                                *
 **********************************************************************************/
static void	trx_make_personalization_string(unsigned char pers[64])
{
	long int	thread_id;
	trx_timespec_t	ts;
	sha512_context	ctx;

	sha512_init(&ctx);
	sha512_starts(&ctx, 1);		/* use SHA-384 mode */

	thread_id = trx_get_thread_id();
	sha512_update(&ctx, (const unsigned char *)&thread_id, sizeof(thread_id));

	trx_timespec(&ts);

	if (0 != ts.ns)
		sha512_update(&ctx, (const unsigned char *)&ts.ns, sizeof(ts.ns));

	sha512_finish(&ctx, pers);
	sha512_free(&ctx);
}
#endif

#if defined(HAVE_POLARSSL)
/******************************************************************************
 *                                                                            *
 * Function: polarssl_debug_cb                                                *
 *                                                                            *
 * Purpose: write a PolarSSL debug message into Treegix log                    *
 *                                                                            *
 * Comments:                                                                  *
 *     Parameter 'tls_ctx' is not used but cannot be removed because this is  *
 *     a callback function, its arguments are defined in PolarSSL.            *
 *                                                                            *
 ******************************************************************************/
static void	polarssl_debug_cb(void *tls_ctx, int level, const char *str)
{
	char	msg[1024];	/* Apparently 1024 bytes is the longest message which can come from PolarSSL 1.3.9 */

	TRX_UNUSED(tls_ctx);

	/* remove '\n' from the end of debug message */
	trx_strlcpy(msg, str, sizeof(msg));
	trx_rtrim(msg, "\n");
	treegix_log(LOG_LEVEL_TRACE, "PolarSSL debug: level=%d \"%s\"", level, msg);
}
#elif defined(HAVE_GNUTLS)
/******************************************************************************
 *                                                                            *
 * Function: trx_gnutls_debug_cb                                              *
 *                                                                            *
 * Purpose: write a GnuTLS debug message into Treegix log                      *
 *                                                                            *
 * Comments:                                                                  *
 *     This is a callback function, its arguments are defined in GnuTLS.      *
 *                                                                            *
 ******************************************************************************/
static void	trx_gnutls_debug_cb(int level, const char *str)
{
	char	msg[1024];

	/* remove '\n' from the end of debug message */
	trx_strlcpy(msg, str, sizeof(msg));
	trx_rtrim(msg, "\n");
	treegix_log(LOG_LEVEL_TRACE, "GnuTLS debug: level=%d \"%s\"", level, msg);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_gnutls_audit_cb                                              *
 *                                                                            *
 * Purpose: write a GnuTLS audit message into Treegix log                      *
 *                                                                            *
 * Comments:                                                                  *
 *     This is a callback function, its arguments are defined in GnuTLS.      *
 *                                                                            *
 ******************************************************************************/
static void	trx_gnutls_audit_cb(gnutls_session_t session, const char *str)
{
	char	msg[1024];

	TRX_UNUSED(session);

	/* remove '\n' from the end of debug message */
	trx_strlcpy(msg, str, sizeof(msg));
	trx_rtrim(msg, "\n");

	treegix_log(LOG_LEVEL_WARNING, "GnuTLS audit: \"%s\"", msg);
}
#endif	/* defined(HAVE_GNUTLS) */

#if defined(HAVE_OPENSSL)
/******************************************************************************
 *                                                                            *
 * Function: trx_openssl_info_cb                                              *
 *                                                                            *
 * Purpose: get state, alert, error information on TLS connection             *
 *                                                                            *
 * Comments:                                                                  *
 *     This is a callback function, its arguments are defined in OpenSSL.     *
 *                                                                            *
 ******************************************************************************/
static void	trx_openssl_info_cb(const SSL *ssl, int where, int ret)
{
	/* There was an idea of using SSL_CB_LOOP and SSL_state_string_long() to write state changes into Treegix log */
	/* if logging level is LOG_LEVEL_TRACE. Unfortunately if OpenSSL for security is compiled without SSLv3 */
	/* (i.e. OPENSSL_NO_SSL3 is defined) then SSL_state_string_long() does not provide descriptions of many */
	/* states anymore. The idea was dropped but the code is here for debugging hard problems. */
#if 0
	if (0 != (where & SSL_CB_LOOP))
	{
		treegix_log(LOG_LEVEL_TRACE, "OpenSSL debug: state=0x%x \"%s\"", (unsigned int)SSL_state(ssl),
				SSL_state_string_long(ssl));
	}
#else
	TRX_UNUSED(ssl);
#endif
	if (0 != (where & SSL_CB_ALERT))	/* alert sent or received */
	{
		const char	*handshake, *direction, *rw;

		if (0 != (where & SSL_CB_EXIT))
			handshake = " handshake";
		else
			handshake = "";

		if (0 != (where & SSL_ST_CONNECT))
			direction = " connect";
		else if (0 != (where & SSL_ST_ACCEPT))
			direction = " accept";
		else
			direction = "";

		if (0 != (where & SSL_CB_READ))
			rw = " read";
		else if (0 != (where & SSL_CB_WRITE))
			rw = " write";
		else
			rw = "";

		trx_snprintf(info_buf, sizeof(info_buf), ": TLS%s%s%s %s alert \"%s\"", handshake, direction, rw,
				SSL_alert_type_string_long(ret), SSL_alert_desc_string_long(ret));
	}
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: trx_tls_error_msg                                                *
 *                                                                            *
 * Purpose: compose a TLS error message                                       *
 *                                                                            *
 ******************************************************************************/
#if defined(HAVE_POLARSSL)
static void	trx_tls_error_msg(int error_code, const char *msg, char **error)
{
	char	err[128];	/* 128 bytes are enough for PolarSSL error messages */

	polarssl_strerror(error_code, err, sizeof(err));
	*error = trx_dsprintf(*error, "%s%s", msg, err);
}
#elif defined(HAVE_OPENSSL)
void	trx_tls_error_msg(char **error, size_t *error_alloc, size_t *error_offset)
{
	unsigned long	error_code;
	const char	*file, *data;
	int		line, flags;
	char		err[1024];

	/* concatenate all error messages in the queue into one string */

	while (0 != (error_code = ERR_get_error_line_data(&file, &line, &data, &flags)))
	{
		ERR_error_string_n(error_code, err, sizeof(err));

		trx_snprintf_alloc(error, error_alloc, error_offset, " file %s line %d: %s", file, line, err);

		if (NULL != data && 0 != (flags & ERR_TXT_STRING))
			trx_snprintf_alloc(error, error_alloc, error_offset, ": %s", data);
	}
}
#endif

#if defined(HAVE_POLARSSL)
/******************************************************************************
 *                                                                            *
 * Function: trx_tls_cert_error_msg                                           *
 *                                                                            *
 * Purpose:                                                                   *
 *     compose a certificate validation error message by decoding PolarSSL    *
 *     ssl_get_verify_result() return value                                   *
 *                                                                            *
 * Parameters:                                                                *
 *     flags   - [IN] result returned by PolarSSL ssl_get_verify_result()     *
 *     error   - [OUT] dynamically allocated memory with error message        *
 *                                                                            *
 ******************************************************************************/
static void	trx_tls_cert_error_msg(unsigned int flags, char **error)
{
	const unsigned int	bits[] = { BADCERT_EXPIRED, BADCERT_REVOKED, BADCERT_CN_MISMATCH,
				BADCERT_NOT_TRUSTED, BADCRL_NOT_TRUSTED,
				BADCRL_EXPIRED, BADCERT_MISSING, BADCERT_SKIP_VERIFY, BADCERT_OTHER,
				BADCERT_FUTURE, BADCRL_FUTURE,
#if 0x01030B00 <= POLARSSL_VERSION_NUMBER	/* 1.3.11 */
				BADCERT_KEY_USAGE, BADCERT_EXT_KEY_USAGE, BADCERT_NS_CERT_TYPE,
#endif
				0 };
	const char		*msgs[] = { "expired", "revoked", "Common Name mismatch",
				"self-signed or not signed by trusted CA", "CRL not signed by trusted CA",
				"CRL expired", "certificate missing", "verification skipped", "other reason",
				"validity starts in future", "CRL validity starts in future"
#if 0x01030B00 <= POLARSSL_VERSION_NUMBER	/* 1.3.11 */
				, "actual use does not match keyUsage extension",
				"actual use does not match extendedKeyUsage extension",
				"actual use does not match nsCertType extension"
#endif
				};
	int			i = 0, k = 0;

	*error = trx_strdup(*error, "invalid peer certificate: ");

	while (0 != flags && 0 != bits[i])
	{
		if (0 != (flags & bits[i]))
		{
			flags &= ~bits[i];	/* reset the checked bit to detect no-more-set-bits without checking */
						/* every bit */
			if (0 != k)
				*error = trx_strdcat(*error, ", ");
			else
				k = 1;

			*error = trx_strdcat(*error, msgs[i]);
		}

		i++;
	}
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: trx_tls_version                                                  *
 *                                                                            *
 * Purpose: print tls library version on stdout by application request with   *
 *          parameter '-V'                                                    *
 *                                                                            *
 ******************************************************************************/
void	trx_tls_version(void)
{
#if defined(HAVE_POLARSSL)
	printf("Compiled with %s\n", POLARSSL_VERSION_STRING_FULL);
#elif defined(HAVE_GNUTLS)
	printf("Compiled with GnuTLS %s\nRunning with GnuTLS %s\n", GNUTLS_VERSION, gnutls_check_version(NULL));
#elif defined(HAVE_OPENSSL)
	printf("This product includes software developed by the OpenSSL Project\n"
			"for use in the OpenSSL Toolkit (http://www.openssl.org/).\n\n");
	printf("Compiled with %s\nRunning with %s\n", OPENSSL_VERSION_TEXT, OpenSSL_version(OPENSSL_VERSION));
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tls_parameter_name                                           *
 *                                                                            *
 * Purpose:                                                                   *
 *     return the name of a configuration file or command line parameter that *
 *     the value of the given parameter comes from                            *
 *                                                                            *
 * Parameters:                                                                *
 *     type  - [IN] type of parameter (file or command line)                  *
 *     param - [IN] address of the global parameter variable                  *
 *                                                                            *
 ******************************************************************************/
#define TRX_TLS_PARAMETER_CONFIG_FILE	0
#define TRX_TLS_PARAMETER_COMMAND_LINE	1
static const char	*trx_tls_parameter_name(int type, char **param)
{
	if (&CONFIG_TLS_CONNECT == param)
		return TRX_TLS_PARAMETER_CONFIG_FILE == type ? "TLSConnect" : "--tls-connect";

	if (&CONFIG_TLS_ACCEPT == param)
		return TRX_TLS_PARAMETER_CONFIG_FILE == type ? "TLSAccept" : "--tls-accept";

	if (&CONFIG_TLS_CA_FILE == param)
		return TRX_TLS_PARAMETER_CONFIG_FILE == type ? "TLSCAFile" : "--tls-ca-file";

	if (&CONFIG_TLS_CRL_FILE == param)
		return TRX_TLS_PARAMETER_CONFIG_FILE == type ? "TLSCRLFile" : "--tls-crl-file";

	if (&CONFIG_TLS_SERVER_CERT_ISSUER == param)
	{
		if (TRX_TLS_PARAMETER_CONFIG_FILE == type)
			return "TLSServerCertIssuer";

		if (0 != (program_type & TRX_PROGRAM_TYPE_GET))
			return "--tls-agent-cert-issuer";
		else
			return "--tls-server-cert-issuer";
	}

	if (&CONFIG_TLS_SERVER_CERT_SUBJECT == param)
	{
		if (TRX_TLS_PARAMETER_CONFIG_FILE == type)
			return "TLSServerCertSubject";

		if (0 != (program_type & TRX_PROGRAM_TYPE_GET))
			return "--tls-agent-cert-subject";
		else
			return "--tls-server-cert-subject";
	}

	if (&CONFIG_TLS_CERT_FILE == param)
		return TRX_TLS_PARAMETER_CONFIG_FILE == type ? "TLSCertFile" : "--tls-cert-file";

	if (&CONFIG_TLS_KEY_FILE == param)
		return TRX_TLS_PARAMETER_CONFIG_FILE == type ? "TLSKeyFile" : "--tls-key-file";

	if (&CONFIG_TLS_PSK_IDENTITY == param)
		return TRX_TLS_PARAMETER_CONFIG_FILE == type ? "TLSPSKIdentity" : "--tls-psk-identity";

	if (&CONFIG_TLS_PSK_FILE == param)
		return TRX_TLS_PARAMETER_CONFIG_FILE == type ? "TLSPSKFile" : "--tls-psk-file";

	THIS_SHOULD_NEVER_HAPPEN;

	trx_tls_free();
	exit(EXIT_FAILURE);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tls_parameter_not_empty                                      *
 *                                                                            *
 * Purpose:                                                                   *
 *     Helper function: check if a configuration parameter is defined it must *
 *     not be empty. Otherwise log error and exit.                            *
 *                                                                            *
 * Parameters:                                                                *
 *     param - [IN] address of the global parameter variable                  *
 *                                                                            *
 ******************************************************************************/
static void	trx_tls_parameter_not_empty(char **param)
{
	const char	*value = *param;

	if (NULL != value)
	{
		while ('\0' != *value)
		{
			if (0 == isspace(*value++))
				return;
		}

		if (0 != (program_type & TRX_PROGRAM_TYPE_SENDER))
		{
			treegix_log(LOG_LEVEL_CRIT, "configuration parameter \"%s\" or \"%s\" is defined but empty",
					trx_tls_parameter_name(TRX_TLS_PARAMETER_CONFIG_FILE, param),
					trx_tls_parameter_name(TRX_TLS_PARAMETER_COMMAND_LINE, param));
		}
		else if (0 != (program_type & TRX_PROGRAM_TYPE_GET))
		{
			treegix_log(LOG_LEVEL_CRIT, "configuration parameter \"%s\" is defined but empty",
					trx_tls_parameter_name(TRX_TLS_PARAMETER_COMMAND_LINE, param));
		}
		else
		{
			treegix_log(LOG_LEVEL_CRIT, "configuration parameter \"%s\" is defined but empty",
					trx_tls_parameter_name(TRX_TLS_PARAMETER_CONFIG_FILE, param));
		}

		trx_tls_free();
		exit(EXIT_FAILURE);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tls_validation_error                                         *
 *                                                                            *
 * Purpose:                                                                   *
 *     Helper function: log error message depending on program type and exit. *
 *                                                                            *
 * Parameters:                                                                *
 *     type   - [IN] type of TLS validation error                             *
 *     param1 - [IN] address of the first global parameter variable           *
 *     param2 - [IN] address of the second global parameter variable (if any) *
 *                                                                            *
 ******************************************************************************/
#define TRX_TLS_VALIDATION_INVALID	0
#define TRX_TLS_VALIDATION_DEPENDENCY	1
#define TRX_TLS_VALIDATION_REQUIREMENT	2
#define TRX_TLS_VALIDATION_UTF8		3
#define TRX_TLS_VALIDATION_NO_PSK	4
static void	trx_tls_validation_error(int type, char **param1, char **param2)
{
	if (TRX_TLS_VALIDATION_INVALID == type)
	{
		if (0 != (program_type & TRX_PROGRAM_TYPE_SENDER))
		{
			treegix_log(LOG_LEVEL_CRIT, "invalid value of \"%s\" or \"%s\" parameter",
					trx_tls_parameter_name(TRX_TLS_PARAMETER_CONFIG_FILE, param1),
					trx_tls_parameter_name(TRX_TLS_PARAMETER_COMMAND_LINE, param1));
		}
		else if (0 != (program_type & TRX_PROGRAM_TYPE_GET))
		{
			treegix_log(LOG_LEVEL_CRIT, "invalid value of \"%s\" parameter",
					trx_tls_parameter_name(TRX_TLS_PARAMETER_COMMAND_LINE, param1));
		}
		else
		{
			treegix_log(LOG_LEVEL_CRIT, "invalid value of \"%s\" parameter",
					trx_tls_parameter_name(TRX_TLS_PARAMETER_CONFIG_FILE, param1));
		}
	}
	else if (TRX_TLS_VALIDATION_DEPENDENCY == type)
	{
		if (0 != (program_type & TRX_PROGRAM_TYPE_SENDER))
		{
			treegix_log(LOG_LEVEL_CRIT, "parameter \"%s\" or \"%s\" is defined,"
					" but neither \"%s\" nor \"%s\" is defined",
					trx_tls_parameter_name(TRX_TLS_PARAMETER_CONFIG_FILE, param1),
					trx_tls_parameter_name(TRX_TLS_PARAMETER_COMMAND_LINE, param1),
					trx_tls_parameter_name(TRX_TLS_PARAMETER_CONFIG_FILE, param2),
					trx_tls_parameter_name(TRX_TLS_PARAMETER_COMMAND_LINE, param2));
		}
		else if (0 != (program_type & TRX_PROGRAM_TYPE_GET))
		{
			treegix_log(LOG_LEVEL_CRIT, "parameter \"%s\" is defined, but \"%s\" is not defined",
					trx_tls_parameter_name(TRX_TLS_PARAMETER_COMMAND_LINE, param1),
					trx_tls_parameter_name(TRX_TLS_PARAMETER_COMMAND_LINE, param2));
		}
		else
		{
			treegix_log(LOG_LEVEL_CRIT, "parameter \"%s\" is defined, but \"%s\" is not defined",
					trx_tls_parameter_name(TRX_TLS_PARAMETER_CONFIG_FILE, param1),
					trx_tls_parameter_name(TRX_TLS_PARAMETER_CONFIG_FILE, param2));
		}
	}
	else if (TRX_TLS_VALIDATION_REQUIREMENT == type)
	{
		if (0 != (program_type & TRX_PROGRAM_TYPE_SENDER))
		{
			treegix_log(LOG_LEVEL_CRIT, "parameter \"%s\" or \"%s\" value requires \"%s\" or \"%s\","
					" but neither of them is defined",
					trx_tls_parameter_name(TRX_TLS_PARAMETER_CONFIG_FILE, param1),
					trx_tls_parameter_name(TRX_TLS_PARAMETER_COMMAND_LINE, param1),
					trx_tls_parameter_name(TRX_TLS_PARAMETER_CONFIG_FILE, param2),
					trx_tls_parameter_name(TRX_TLS_PARAMETER_COMMAND_LINE, param2));
		}
		else if (0 != (program_type & TRX_PROGRAM_TYPE_GET))
		{
			treegix_log(LOG_LEVEL_CRIT, "parameter \"%s\" value requires \"%s\", but it is not defined",
					trx_tls_parameter_name(TRX_TLS_PARAMETER_COMMAND_LINE, param1),
					trx_tls_parameter_name(TRX_TLS_PARAMETER_COMMAND_LINE, param2));
		}
		else
		{
			treegix_log(LOG_LEVEL_CRIT, "parameter \"%s\" value requires \"%s\", but it is not defined",
					trx_tls_parameter_name(TRX_TLS_PARAMETER_CONFIG_FILE, param1),
					trx_tls_parameter_name(TRX_TLS_PARAMETER_CONFIG_FILE, param2));
		}
	}
	else if (TRX_TLS_VALIDATION_UTF8 == type)
	{
		if (0 != (program_type & TRX_PROGRAM_TYPE_SENDER))
		{
			treegix_log(LOG_LEVEL_CRIT, "parameter \"%s\" or \"%s\" value is not a valid UTF-8 string",
					trx_tls_parameter_name(TRX_TLS_PARAMETER_CONFIG_FILE, param1),
					trx_tls_parameter_name(TRX_TLS_PARAMETER_COMMAND_LINE, param1));
		}
		else if (0 != (program_type & TRX_PROGRAM_TYPE_GET))
		{
			treegix_log(LOG_LEVEL_CRIT, "parameter \"%s\" value is not a valid UTF-8 string",
					trx_tls_parameter_name(TRX_TLS_PARAMETER_COMMAND_LINE, param1));
		}
		else
		{
			treegix_log(LOG_LEVEL_CRIT, "parameter \"%s\" value is not a valid UTF-8 string",
					trx_tls_parameter_name(TRX_TLS_PARAMETER_CONFIG_FILE, param1));
		}
	}
	else if (TRX_TLS_VALIDATION_NO_PSK == type)
	{
		if (0 != (program_type & TRX_PROGRAM_TYPE_SENDER))
		{
			treegix_log(LOG_LEVEL_CRIT, "value of parameter \"%s\" or \"%s\" requires support of encrypted"
					" connection with PSK but support for PSK was not compiled in",
					trx_tls_parameter_name(TRX_TLS_PARAMETER_CONFIG_FILE, param1),
					trx_tls_parameter_name(TRX_TLS_PARAMETER_COMMAND_LINE, param1));
		}
		else if (0 != (program_type & TRX_PROGRAM_TYPE_GET))
		{
			treegix_log(LOG_LEVEL_CRIT, "value of parameter \"%s\" requires support of encrypted"
					" connection with PSK but support for PSK was not compiled in",
					trx_tls_parameter_name(TRX_TLS_PARAMETER_COMMAND_LINE, param1));
		}
		else
		{
			treegix_log(LOG_LEVEL_CRIT, "value of parameter \"%s\" requires support of encrypted"
					" connection with PSK but support for PSK was not compiled in",
					trx_tls_parameter_name(TRX_TLS_PARAMETER_CONFIG_FILE, param1));
		}
	}
	else
		THIS_SHOULD_NEVER_HAPPEN;

	trx_tls_free();
	exit(EXIT_FAILURE);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tls_validate_config                                          *
 *                                                                            *
 * Purpose: check for allowed combinations of TLS configuration parameters    *
 *                                                                            *
 * Comments:                                                                  *
 *     Valid combinations:                                                    *
 *         - either all 3 certificate parameters - CONFIG_TLS_CERT_FILE,      *
 *           CONFIG_TLS_KEY_FILE, CONFIG_TLS_CA_FILE  - are defined and not   *
 *           empty or none of them. Parameter CONFIG_TLS_CRL_FILE is optional *
 *           but may be defined only together with the 3 certificate          *
 *           parameters,                                                      *
 *         - either both PSK parameters - CONFIG_TLS_PSK_IDENTITY and         *
 *           CONFIG_TLS_PSK_FILE - are defined and not empty or none of them, *
 *           (if CONFIG_TLS_PSK_IDENTITY is defined it must be a valid UTF-8  *
 *           string),                                                         *
 *         - in active agent, active proxy, treegix_get, and treegix_sender the *
 *           certificate and PSK parameters must match the value of           *
 *           CONFIG_TLS_CONNECT parameter,                                    *
 *         - in passive agent and passive proxy the certificate and PSK       *
 *           parameters must match the value of CONFIG_TLS_ACCEPT parameter.  *
 *                                                                            *
 ******************************************************************************/
void	trx_tls_validate_config(void)
{
	trx_tls_parameter_not_empty(&CONFIG_TLS_CONNECT);
	trx_tls_parameter_not_empty(&CONFIG_TLS_ACCEPT);
	trx_tls_parameter_not_empty(&CONFIG_TLS_CA_FILE);
	trx_tls_parameter_not_empty(&CONFIG_TLS_CRL_FILE);
	trx_tls_parameter_not_empty(&CONFIG_TLS_SERVER_CERT_ISSUER);
	trx_tls_parameter_not_empty(&CONFIG_TLS_SERVER_CERT_SUBJECT);
	trx_tls_parameter_not_empty(&CONFIG_TLS_CERT_FILE);
	trx_tls_parameter_not_empty(&CONFIG_TLS_KEY_FILE);
	trx_tls_parameter_not_empty(&CONFIG_TLS_PSK_IDENTITY);
	trx_tls_parameter_not_empty(&CONFIG_TLS_PSK_FILE);

	/* parse and validate 'TLSConnect' parameter (in treegix_proxy.conf, treegix_agentd.conf) and '--tls-connect' */
	/* parameter (in treegix_get and treegix_sender) */

	if (NULL != CONFIG_TLS_CONNECT)
	{
		/* 'configured_tls_connect_mode' is shared between threads on MS Windows */

		if (0 == strcmp(CONFIG_TLS_CONNECT, TRX_TCP_SEC_UNENCRYPTED_TXT))
			configured_tls_connect_mode = TRX_TCP_SEC_UNENCRYPTED;
		else if (0 == strcmp(CONFIG_TLS_CONNECT, TRX_TCP_SEC_TLS_CERT_TXT))
			configured_tls_connect_mode = TRX_TCP_SEC_TLS_CERT;
		else if (0 == strcmp(CONFIG_TLS_CONNECT, TRX_TCP_SEC_TLS_PSK_TXT))
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || (defined(HAVE_OPENSSL) && defined(HAVE_OPENSSL_WITH_PSK))
			configured_tls_connect_mode = TRX_TCP_SEC_TLS_PSK;
#else
			trx_tls_validation_error(TRX_TLS_VALIDATION_NO_PSK, &CONFIG_TLS_CONNECT, NULL);
#endif
		else
			trx_tls_validation_error(TRX_TLS_VALIDATION_INVALID, &CONFIG_TLS_CONNECT, NULL);
	}

	/* parse and validate 'TLSAccept' parameter (in treegix_proxy.conf, treegix_agentd.conf) */

	if (NULL != CONFIG_TLS_ACCEPT)
	{
		char		*s, *p, *delim;
		unsigned int	accept_modes_tmp = 0;	/* 'configured_tls_accept_modes' is shared between threads on */
							/* MS Windows. To avoid races make a local temporary */
							/* variable, modify it and write into */
							/* 'configured_tls_accept_modes' when done. */

		p = s = trx_strdup(NULL, CONFIG_TLS_ACCEPT);

		while (1)
		{
			delim = strchr(p, ',');

			if (NULL != delim)
				*delim = '\0';

			if (0 == strcmp(p, TRX_TCP_SEC_UNENCRYPTED_TXT))
				accept_modes_tmp |= TRX_TCP_SEC_UNENCRYPTED;
			else if (0 == strcmp(p, TRX_TCP_SEC_TLS_CERT_TXT))
				accept_modes_tmp |= TRX_TCP_SEC_TLS_CERT;
			else if (0 == strcmp(p, TRX_TCP_SEC_TLS_PSK_TXT))
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || (defined(HAVE_OPENSSL) && defined(HAVE_OPENSSL_WITH_PSK))
				accept_modes_tmp |= TRX_TCP_SEC_TLS_PSK;
#else
				trx_tls_validation_error(TRX_TLS_VALIDATION_NO_PSK, &CONFIG_TLS_ACCEPT, NULL);
#endif
			else
			{
				trx_free(s);
				trx_tls_validation_error(TRX_TLS_VALIDATION_INVALID, &CONFIG_TLS_ACCEPT, NULL);
			}

			if (NULL == delim)
				break;

			p = delim + 1;
		}

		configured_tls_accept_modes = accept_modes_tmp;

		trx_free(s);
	}

	/* either both a certificate and a private key must be defined or none of them */

	if (NULL != CONFIG_TLS_CERT_FILE && NULL == CONFIG_TLS_KEY_FILE)
		trx_tls_validation_error(TRX_TLS_VALIDATION_DEPENDENCY, &CONFIG_TLS_CERT_FILE, &CONFIG_TLS_KEY_FILE);

	if (NULL != CONFIG_TLS_KEY_FILE && NULL == CONFIG_TLS_CERT_FILE)
		trx_tls_validation_error(TRX_TLS_VALIDATION_DEPENDENCY, &CONFIG_TLS_KEY_FILE, &CONFIG_TLS_CERT_FILE);

	/* CA file must be defined only together with a certificate */

	if (NULL != CONFIG_TLS_CERT_FILE && NULL == CONFIG_TLS_CA_FILE)
		trx_tls_validation_error(TRX_TLS_VALIDATION_DEPENDENCY, &CONFIG_TLS_CERT_FILE, &CONFIG_TLS_CA_FILE);

	if (NULL != CONFIG_TLS_CA_FILE && NULL == CONFIG_TLS_CERT_FILE)
		trx_tls_validation_error(TRX_TLS_VALIDATION_DEPENDENCY, &CONFIG_TLS_CA_FILE, &CONFIG_TLS_CERT_FILE);

	/* CRL file is optional but must be defined only together with a certificate */

	if (NULL == CONFIG_TLS_CERT_FILE && NULL != CONFIG_TLS_CRL_FILE)
		trx_tls_validation_error(TRX_TLS_VALIDATION_DEPENDENCY, &CONFIG_TLS_CRL_FILE, &CONFIG_TLS_CERT_FILE);

	/* Server certificate issuer is optional but must be defined only together with a certificate */

	if (NULL == CONFIG_TLS_CERT_FILE && NULL != CONFIG_TLS_SERVER_CERT_ISSUER)
	{
		trx_tls_validation_error(TRX_TLS_VALIDATION_DEPENDENCY, &CONFIG_TLS_SERVER_CERT_ISSUER,
				&CONFIG_TLS_CERT_FILE);
	}

	/* Server certificate subject is optional but must be defined only together with a certificate */

	if (NULL == CONFIG_TLS_CERT_FILE && NULL != CONFIG_TLS_SERVER_CERT_SUBJECT)
	{
		trx_tls_validation_error(TRX_TLS_VALIDATION_DEPENDENCY, &CONFIG_TLS_SERVER_CERT_SUBJECT,
				&CONFIG_TLS_CERT_FILE);
	}

	/* either both a PSK and a PSK identity must be defined or none of them */

	if (NULL != CONFIG_TLS_PSK_FILE && NULL == CONFIG_TLS_PSK_IDENTITY)
		trx_tls_validation_error(TRX_TLS_VALIDATION_DEPENDENCY, &CONFIG_TLS_PSK_FILE, &CONFIG_TLS_PSK_IDENTITY);

	if (NULL != CONFIG_TLS_PSK_IDENTITY && NULL == CONFIG_TLS_PSK_FILE)
		trx_tls_validation_error(TRX_TLS_VALIDATION_DEPENDENCY, &CONFIG_TLS_PSK_IDENTITY, &CONFIG_TLS_PSK_FILE);

	/* PSK identity must be a valid UTF-8 string (RFC 4279 says Unicode) */
	if (NULL != CONFIG_TLS_PSK_IDENTITY && SUCCEED != trx_is_utf8(CONFIG_TLS_PSK_IDENTITY))
		trx_tls_validation_error(TRX_TLS_VALIDATION_UTF8, &CONFIG_TLS_PSK_IDENTITY, NULL);

	/* active agentd, active proxy, treegix_get, and treegix_sender specific validation */

	if ((0 != (program_type & TRX_PROGRAM_TYPE_AGENTD) && 0 != CONFIG_ACTIVE_FORKS) ||
			(0 != (program_type & (TRX_PROGRAM_TYPE_PROXY_ACTIVE | TRX_PROGRAM_TYPE_GET |
					TRX_PROGRAM_TYPE_SENDER))))
	{
		/* 'TLSConnect' is the master parameter to be matched by certificate and PSK parameters. */

		if (NULL != CONFIG_TLS_CERT_FILE && NULL == CONFIG_TLS_CONNECT)
		{
			trx_tls_validation_error(TRX_TLS_VALIDATION_DEPENDENCY, &CONFIG_TLS_CERT_FILE,
					&CONFIG_TLS_CONNECT);
		}

		if (NULL != CONFIG_TLS_PSK_FILE && NULL == CONFIG_TLS_CONNECT)
		{
			trx_tls_validation_error(TRX_TLS_VALIDATION_DEPENDENCY, &CONFIG_TLS_PSK_FILE,
					&CONFIG_TLS_CONNECT);
		}

		if (0 != (configured_tls_connect_mode & TRX_TCP_SEC_TLS_CERT) && NULL == CONFIG_TLS_CERT_FILE)
		{
			trx_tls_validation_error(TRX_TLS_VALIDATION_REQUIREMENT, &CONFIG_TLS_CONNECT,
					&CONFIG_TLS_CERT_FILE);
		}

		if (0 != (configured_tls_connect_mode & TRX_TCP_SEC_TLS_PSK) && NULL == CONFIG_TLS_PSK_FILE)
		{
			trx_tls_validation_error(TRX_TLS_VALIDATION_REQUIREMENT, &CONFIG_TLS_CONNECT,
					&CONFIG_TLS_PSK_FILE);
		}
	}

	/* passive agentd and passive proxy specific validation */

	if ((0 != (program_type & TRX_PROGRAM_TYPE_AGENTD) && 0 != CONFIG_PASSIVE_FORKS) ||
			0 != (program_type & TRX_PROGRAM_TYPE_PROXY_PASSIVE))
	{
		/* 'TLSAccept' is the master parameter to be matched by certificate and PSK parameters */

		if (NULL != CONFIG_TLS_CERT_FILE && NULL == CONFIG_TLS_ACCEPT)
		{
			trx_tls_validation_error(TRX_TLS_VALIDATION_DEPENDENCY, &CONFIG_TLS_CERT_FILE,
					&CONFIG_TLS_ACCEPT);
		}

		if (NULL != CONFIG_TLS_PSK_FILE && NULL == CONFIG_TLS_ACCEPT)
		{
			trx_tls_validation_error(TRX_TLS_VALIDATION_DEPENDENCY, &CONFIG_TLS_PSK_FILE,
					&CONFIG_TLS_ACCEPT);
		}

		if (0 != (configured_tls_accept_modes & TRX_TCP_SEC_TLS_CERT) && NULL == CONFIG_TLS_CERT_FILE)
		{
			trx_tls_validation_error(TRX_TLS_VALIDATION_REQUIREMENT, &CONFIG_TLS_ACCEPT,
					&CONFIG_TLS_CERT_FILE);
		}

		if (0 != (configured_tls_accept_modes & TRX_TCP_SEC_TLS_PSK) && NULL == CONFIG_TLS_PSK_FILE)
		{
			trx_tls_validation_error(TRX_TLS_VALIDATION_REQUIREMENT, &CONFIG_TLS_ACCEPT,
					&CONFIG_TLS_PSK_FILE);
		}
	}
}

#if defined(HAVE_POLARSSL)
/******************************************************************************
 *                                                                            *
 * Function: trx_is_ciphersuite_cert                                          *
 *                                                                            *
 * Purpose: does the specified ciphersuite ID refer to a non-PSK              *
 *          (i.e. certificate) ciphersuite supported for the specified TLS    *
 *          version range                                                     *
 *                                                                            *
 * Comments:                                                                  *
 *          RFC 7465 "Prohibiting RC4 Cipher Suites" requires that RC4 should *
 *          never be used. Also, discard weak encryptions.                    *
 *                                                                            *
 ******************************************************************************/
static int	trx_is_ciphersuite_cert(const int *p)
{
	const ssl_ciphersuite_t	*info;

	/* PolarSSL function ssl_ciphersuite_uses_psk() is not used here because it can be unavailable in some */
	/* installations. */
	if (NULL != (info = ssl_ciphersuite_from_id(*p)) && (POLARSSL_KEY_EXCHANGE_ECDHE_RSA == info->key_exchange ||
			POLARSSL_KEY_EXCHANGE_RSA == info->key_exchange) &&
			(POLARSSL_CIPHER_AES_128_GCM == info->cipher || POLARSSL_CIPHER_AES_128_CBC == info->cipher) &&
			0 == (info->flags & POLARSSL_CIPHERSUITE_WEAK) &&
			(TRX_TLS_MIN_MAJOR_VER > info->min_major_ver || (TRX_TLS_MIN_MAJOR_VER == info->min_major_ver &&
			TRX_TLS_MIN_MINOR_VER >= info->min_minor_ver)) &&
			(TRX_TLS_MAX_MAJOR_VER < info->max_major_ver || (TRX_TLS_MAX_MAJOR_VER == info->max_major_ver &&
			TRX_TLS_MAX_MINOR_VER <= info->max_minor_ver)))
	{
		return SUCCEED;
	}
	else
		return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_is_ciphersuite_psk                                           *
 *                                                                            *
 * Purpose: does the specified ciphersuite ID refer to a PSK ciphersuite      *
 *          supported for the specified TLS version range                     *
 *                                                                            *
 * Comments:                                                                  *
 *          RFC 7465 "Prohibiting RC4 Cipher Suites" requires that RC4 should *
 *          never be used. Also, discard weak encryptions.                    *
 *                                                                            *
 ******************************************************************************/
static int	trx_is_ciphersuite_psk(const int *p)
{
	const ssl_ciphersuite_t	*info;

	if (NULL != (info = ssl_ciphersuite_from_id(*p)) && (POLARSSL_KEY_EXCHANGE_ECDHE_PSK == info->key_exchange ||
			POLARSSL_KEY_EXCHANGE_PSK == info->key_exchange) &&
			(POLARSSL_CIPHER_AES_128_GCM == info->cipher || POLARSSL_CIPHER_AES_128_CBC == info->cipher) &&
			0 == (info->flags & POLARSSL_CIPHERSUITE_WEAK) &&
			(TRX_TLS_MIN_MAJOR_VER > info->min_major_ver || (TRX_TLS_MIN_MAJOR_VER == info->min_major_ver &&
			TRX_TLS_MIN_MINOR_VER >= info->min_minor_ver)) &&
			(TRX_TLS_MAX_MAJOR_VER < info->max_major_ver || (TRX_TLS_MAX_MAJOR_VER == info->max_major_ver &&
			TRX_TLS_MAX_MINOR_VER <= info->max_minor_ver)))
	{
		return SUCCEED;
	}
	else
		return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_is_ciphersuite_all                                           *
 *                                                                            *
 * Purpose: does the specified ciphersuite ID refer to a good ciphersuite     *
 *          supported for the specified TLS version range                     *
 *                                                                            *
 * Comments:                                                                  *
 *          RFC 7465 "Prohibiting RC4 Cipher Suites" requires that RC4 should *
 *          never be used. Also, discard weak encryptions.                    *
 *                                                                            *
 ******************************************************************************/
static int	trx_is_ciphersuite_all(const int *p)
{
	const ssl_ciphersuite_t	*info;

	if (NULL != (info = ssl_ciphersuite_from_id(*p)) && (POLARSSL_KEY_EXCHANGE_ECDHE_RSA == info->key_exchange ||
			POLARSSL_KEY_EXCHANGE_RSA == info->key_exchange ||
			POLARSSL_KEY_EXCHANGE_ECDHE_PSK == info->key_exchange ||
			POLARSSL_KEY_EXCHANGE_PSK == info->key_exchange) &&
			(POLARSSL_CIPHER_AES_128_GCM == info->cipher || POLARSSL_CIPHER_AES_128_CBC == info->cipher) &&
			0 == (info->flags & POLARSSL_CIPHERSUITE_WEAK) &&
			(TRX_TLS_MIN_MAJOR_VER > info->min_major_ver || (TRX_TLS_MIN_MAJOR_VER == info->min_major_ver &&
			TRX_TLS_MIN_MINOR_VER >= info->min_minor_ver)) &&
			(TRX_TLS_MAX_MAJOR_VER < info->max_major_ver || (TRX_TLS_MAX_MAJOR_VER == info->max_major_ver &&
			TRX_TLS_MAX_MINOR_VER <= info->max_minor_ver)))
	{
		return SUCCEED;
	}
	else
		return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_ciphersuites                                                 *
 *                                                                            *
 * Purpose: copy a list of ciphersuites (certificate or PSK-related) from a   *
 *          list of all supported ciphersuites                                *
 *                                                                            *
 ******************************************************************************/
static unsigned int	trx_ciphersuites(int type, int **suites)
{
	const int	*supported_suites, *p;
	int		*q;
	unsigned int	count = 0;

	supported_suites = ssl_list_ciphersuites();

	/* count available relevant ciphersuites */
	for (p = supported_suites; 0 != *p; p++)
	{
		if (TRX_TLS_CIPHERSUITE_CERT == type)
		{
			if (SUCCEED != trx_is_ciphersuite_cert(p))
				continue;
		}
		else if (TRX_TLS_CIPHERSUITE_PSK == type)
		{
			if (SUCCEED != trx_is_ciphersuite_psk(p))
				continue;
		}
		else	/* TRX_TLS_CIPHERSUITE_ALL */
		{
			if (SUCCEED != trx_is_ciphersuite_all(p))
				continue;
		}

		count++;
	}

	*suites = trx_malloc(*suites, (count + 1) * sizeof(int));

	/* copy the ciphersuites into array */
	for (p = supported_suites, q = *suites; 0 != *p; p++)
	{
		if (TRX_TLS_CIPHERSUITE_CERT == type)
		{
			if (SUCCEED != trx_is_ciphersuite_cert(p))
				continue;
		}
		else if (TRX_TLS_CIPHERSUITE_PSK == type)
		{
			if (SUCCEED != trx_is_ciphersuite_psk(p))
				continue;
		}
		else	/* TRX_TLS_CIPHERSUITE_ALL */
		{
			if (SUCCEED != trx_is_ciphersuite_all(p))
				continue;
		}

		*q++ = *p;
	}

	*q = 0;

	return count;
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: trx_psk_hex2bin                                                  *
 *                                                                            *
 * Purpose:                                                                   *
 *     convert a pre-shared key from a textual representation (ASCII hex      *
 *     digit string) to a binary representation (byte string)                 *
 *                                                                            *
 * Parameters:                                                                *
 *     p_hex   - [IN] null-terminated input PSK hex-string                    *
 *     buf     - [OUT] output buffer                                          *
 *     buf_len - [IN] output buffer size                                      *
 *                                                                            *
 * Return value:                                                              *
 *     Number of PSK bytes written into 'buf' on successful conversion.       *
 *     -1 - an error occurred.                                                *
 *                                                                            *
 * Comments:                                                                  *
 *     In case of error incomplete useless data may be written into 'buf'.    *
 *                                                                            *
 ******************************************************************************/
static int	trx_psk_hex2bin(const unsigned char *p_hex, unsigned char *buf, int buf_len)
{
	unsigned char	*q, hi, lo;
	int		len = 0;

	q = buf;

	while ('\0' != *p_hex)
	{
		if (0 != isxdigit(*p_hex) && 0 != isxdigit(*(p_hex + 1)) && buf_len > len)
		{
			hi = *p_hex & 0x0f;

			if ('9' < *p_hex++)
				hi += 9u;

			lo = *p_hex & 0x0f;

			if ('9' < *p_hex++)
				lo += 9u;

			*q++ = hi << 4 | lo;
			len++;
		}
		else
			return -1;
	}

	return len;
}

static void	trx_psk_warn_misconfig(const char *psk_identity)
{
	treegix_log(LOG_LEVEL_WARNING, "same PSK identity \"%s\" but different PSK values used in proxy configuration"
			" file, for host or for autoregistration; autoregistration will not be allowed", psk_identity);
}

#if defined(HAVE_POLARSSL)
/******************************************************************************
 *                                                                            *
 * Function: trx_psk_cb                                                       *
 *                                                                            *
 * Purpose:                                                                   *
 *     find and set the requested pre-shared key upon PolarSSL request        *
 *                                                                            *
 * Parameters:                                                                *
 *     par              - [IN] not used                                       *
 *     tls_ctx          - [IN] TLS connection context                         *
 *     psk_identity     - [IN] PSK identity for which the PSK should be       *
 *                             searched and set                               *
 *     psk_identity_len - [IN] size of 'psk_identity'                         *
 *                                                                            *
 * Return value:                                                              *
 *     0  - required PSK successfully found and set                           *
 *     -1 - an error occurred                                                 *
 *                                                                            *
 * Comments:                                                                  *
 *     A callback function, its arguments are defined in PolarSSL.            *
 *     Used only in server and proxy.                                         *
 *                                                                            *
 ******************************************************************************/
static int	trx_psk_cb(void *par, ssl_context *tls_ctx, const unsigned char *psk_identity,
		size_t psk_identity_len)
{
	unsigned char	*psk;
	size_t		psk_len = 0;
	int		psk_bin_len;
	unsigned char	tls_psk_identity[HOST_TLS_PSK_IDENTITY_LEN_MAX], tls_psk_hex[HOST_TLS_PSK_LEN_MAX],
			psk_buf[HOST_TLS_PSK_LEN / 2];

	TRX_UNUSED(par);

	/* special print: psk_identity is not '\0'-terminated */
	treegix_log(LOG_LEVEL_DEBUG, "%s() requested PSK identity \"%.*s\"", __func__, (int)psk_identity_len,
			psk_identity);

	psk_usage = 0;

	if (HOST_TLS_PSK_IDENTITY_LEN < psk_identity_len)
	{
		THIS_SHOULD_NEVER_HAPPEN;
		return -1;
	}

	memcpy(tls_psk_identity, psk_identity, psk_identity_len);
	tls_psk_identity[psk_identity_len] = '\0';

	/* call the function DCget_psk_by_identity() by pointer */
	if (0 < find_psk_in_cache(tls_psk_identity, tls_psk_hex, &psk_usage))
	{
		/* The PSK is in configuration cache. Convert PSK to binary form. */
		if (0 >= (psk_bin_len = trx_psk_hex2bin(tls_psk_hex, psk_buf, sizeof(psk_buf))))
		{
			/* this should have been prevented by validation in frontend or API */
			treegix_log(LOG_LEVEL_WARNING, "cannot convert PSK to binary form for PSK identity"
					" \"%.*s\"", (int)psk_identity_len, psk_identity);
			return -1;
		}

		psk = psk_buf;
		psk_len = (size_t)psk_bin_len;
	}

	if (0 != (program_type & TRX_PROGRAM_TYPE_PROXY) &&
			0 < my_psk_identity_len &&
			my_psk_identity_len == psk_identity_len &&
			0 == memcmp(my_psk_identity, psk_identity, psk_identity_len))
	{
		/* the PSK is in proxy configuration file */
		psk_usage |= TRX_PSK_FOR_PROXY;

		if (0 < psk_len && (psk_len != my_psk_len || 0 != memcmp(psk, my_psk, psk_len)))
		{
			/* PSK was also found in configuration cache but with different value */
			trx_psk_warn_misconfig((const char *)psk_identity);
			psk_usage &= ~(unsigned int)TRX_PSK_FOR_AUTOREG;
		}

		psk = (unsigned char *)my_psk;	/* prefer PSK from proxy configuration file */
		psk_len = my_psk_len;
	}

	if (0 == psk_len)
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot find requested PSK identity \"%.*s\"",
				(int)psk_identity_len, psk_identity);
		return -1;
	}

	if (0 < psk_len)
	{
		int 	res;

		if (0 == (res = ssl_set_psk(tls_ctx, psk, psk_len, psk_identity, psk_identity_len)))
			return 0;

		trx_tls_error_msg(res, "", &err_msg);
		treegix_log(LOG_LEVEL_WARNING, "cannot set PSK for PSK identity \"%.*s\": %s", (int)psk_identity_len,
				psk_identity, err_msg);
		trx_free(err_msg);
	}

	return -1;
}
#elif defined(HAVE_GNUTLS)
/******************************************************************************
 *                                                                            *
 * Function: trx_psk_cb                                                       *
 *                                                                            *
 * Purpose:                                                                   *
 *     find and set the requested pre-shared key upon GnuTLS request          *
 *                                                                            *
 * Parameters:                                                                *
 *     session      - [IN] not used                                           *
 *     psk_identity - [IN] PSK identity for which the PSK should be searched  *
 *                         and set                                            *
 *     key          - [OUT pre-shared key allocated and set                   *
 *                                                                            *
 * Return value:                                                              *
 *     0  - required PSK successfully found and set                           *
 *     -1 - an error occurred                                                 *
 *                                                                            *
 * Comments:                                                                  *
 *     A callback function, its arguments are defined in GnuTLS.              *
 *     Used in all programs accepting connections.                            *
 *                                                                            *
 ******************************************************************************/
static int	trx_psk_cb(gnutls_session_t session, const char *psk_identity, gnutls_datum_t *key)
{
	char		*psk;
	size_t		psk_len = 0;
	int		psk_bin_len;
	unsigned char	tls_psk_hex[HOST_TLS_PSK_LEN_MAX], psk_buf[HOST_TLS_PSK_LEN / 2];

	TRX_UNUSED(session);

	treegix_log(LOG_LEVEL_DEBUG, "%s() requested PSK identity \"%s\"", __func__, psk_identity);

	psk_usage = 0;

	if (0 != (program_type & (TRX_PROGRAM_TYPE_PROXY | TRX_PROGRAM_TYPE_SERVER)))
	{
		/* call the function DCget_psk_by_identity() by pointer */
		if (0 < find_psk_in_cache((const unsigned char *)psk_identity, tls_psk_hex, &psk_usage))
		{
			/* The PSK is in configuration cache. Convert PSK to binary form. */
			if (0 >= (psk_bin_len = trx_psk_hex2bin(tls_psk_hex, psk_buf, sizeof(psk_buf))))
			{
				/* this should have been prevented by validation in frontend or API */
				treegix_log(LOG_LEVEL_WARNING, "cannot convert PSK to binary form for PSK identity"
						" \"%s\"", psk_identity);
				return -1;	/* fail */
			}

			psk = (char *)psk_buf;
			psk_len = (size_t)psk_bin_len;
		}

		if (0 != (program_type & TRX_PROGRAM_TYPE_PROXY) &&
				0 < my_psk_identity_len &&
				0 == strcmp(my_psk_identity, psk_identity))
		{
			/* the PSK is in proxy configuration file */
			psk_usage |= TRX_PSK_FOR_PROXY;

			if (0 < psk_len && (psk_len != my_psk_len || 0 != memcmp(psk, my_psk, psk_len)))
			{
				/* PSK was also found in configuration cache but with different value */
				trx_psk_warn_misconfig(psk_identity);
				psk_usage &= ~(unsigned int)TRX_PSK_FOR_AUTOREG;
			}

			psk = my_psk;	/* prefer PSK from proxy configuration file */
			psk_len = my_psk_len;
		}

		if (0 == psk_len)
		{
			treegix_log(LOG_LEVEL_WARNING, "cannot find requested PSK identity \"%s\"", psk_identity);
			return -1;	/* fail */
		}
	}
	else if (0 != (program_type & TRX_PROGRAM_TYPE_AGENTD))
	{
		if (0 < my_psk_identity_len)
		{
			if (0 == strcmp(my_psk_identity, psk_identity))
			{
				psk = my_psk;
				psk_len = my_psk_len;
			}
			else
			{
				treegix_log(LOG_LEVEL_WARNING, "cannot find requested PSK identity \"%s\", available PSK"
						" identity \"%s\"", psk_identity, my_psk_identity);
				return -1;	/* fail */
			}
		}
	}

	if (0 < psk_len)
	{
		if (NULL == (key->data = gnutls_malloc(psk_len)))
		{
			treegix_log(LOG_LEVEL_WARNING, "cannot allocate " TRX_FS_SIZE_T " bytes of memory for PSK with"
					" identity \"%s\"", (trx_fs_size_t)psk_len, psk_identity);
			return -1;	/* fail */
		}

		memcpy(key->data, psk, psk_len);
		key->size = (unsigned int)psk_len;

		return 0;	/* success */
	}

	return -1;	/* PSK not found */
}
#elif defined(HAVE_OPENSSL) && defined(HAVE_OPENSSL_WITH_PSK)
/******************************************************************************
 *                                                                            *
 * Function: trx_psk_client_cb                                                *
 *                                                                            *
 * Purpose:                                                                   *
 *     set pre-shared key for outgoing TLS connection upon OpenSSL request    *
 *                                                                            *
 * Parameters:                                                                *
 *     ssl              - [IN] not used                                       *
 *     hint             - [IN] not used                                       *
 *     identity         - [OUT] buffer to write PSK identity into             *
 *     max_identity_len - [IN] size of the 'identity' buffer                  *
 *     psk              - [OUT] buffer to write PSK into                      *
 *     max_psk_len      - [IN] size of the 'psk' buffer                       *
 *                                                                            *
 * Return value:                                                              *
 *     > 0 - length of PSK in bytes                                           *
 *       0 - an error occurred                                                *
 *                                                                            *
 * Comments:                                                                  *
 *     A callback function, its arguments are defined in OpenSSL.             *
 *     Used in all programs making outgoing TLS PSK connections.              *
 *                                                                            *
 *     As a client we use different PSKs depending on connection to be made.  *
 *     Apparently there is no simple way to specify which PSK should be set   *
 *     by this callback function. We use global variables to pass this info.  *
 *                                                                            *
 ******************************************************************************/
static unsigned int	trx_psk_client_cb(SSL *ssl, const char *hint, char *identity,
		unsigned int max_identity_len, unsigned char *psk, unsigned int max_psk_len)
{
	TRX_UNUSED(ssl);
	TRX_UNUSED(hint);

	treegix_log(LOG_LEVEL_DEBUG, "%s() requested PSK identity \"%s\"", __func__, psk_identity_for_cb);

	if (max_identity_len < psk_identity_len_for_cb + 1)	/* 1 byte for terminating '\0' */
	{
		treegix_log(LOG_LEVEL_WARNING, "requested PSK identity \"%s\" does not fit into %u-byte buffer",
				psk_identity_for_cb, max_identity_len);
		return 0;
	}

	if (max_psk_len < psk_len_for_cb)
	{
		treegix_log(LOG_LEVEL_WARNING, "PSK associated with PSK identity \"%s\" does not fit into %u-byte"
				" buffer", psk_identity_for_cb, max_psk_len);
		return 0;
	}

	trx_strlcpy(identity, psk_identity_for_cb, max_identity_len);
	memcpy(psk, psk_for_cb, psk_len_for_cb);

	return (unsigned int)psk_len_for_cb;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_psk_server_cb                                                *
 *                                                                            *
 * Purpose:                                                                   *
 *     set pre-shared key for incoming TLS connection upon OpenSSL request    *
 *                                                                            *
 * Parameters:                                                                *
 *     ssl              - [IN] not used                                       *
 *     identity         - [IN] PSK identity sent by client                    *
 *     psk              - [OUT] buffer to write PSK into                      *
 *     max_psk_len      - [IN] size of the 'psk' buffer                       *
 *                                                                            *
 * Return value:                                                              *
 *     > 0 - length of PSK in bytes                                           *
 *       0 - PSK identity not found                                           *
 *                                                                            *
 * Comments:                                                                  *
 *     A callback function, its arguments are defined in OpenSSL.             *
 *     Used in all programs accepting incoming TLS PSK connections.           *
 *                                                                            *
 ******************************************************************************/
static unsigned int	trx_psk_server_cb(SSL *ssl, const char *identity, unsigned char *psk,
		unsigned int max_psk_len)
{
	char		*psk_loc;
	size_t		psk_len = 0;
	int		psk_bin_len;
	unsigned char	tls_psk_hex[HOST_TLS_PSK_LEN_MAX], psk_buf[HOST_TLS_PSK_LEN / 2];

	TRX_UNUSED(ssl);

	treegix_log(LOG_LEVEL_DEBUG, "%s() requested PSK identity \"%s\"", __func__, identity);

	incoming_connection_has_psk = 1;
	psk_usage = 0;

	if (0 != (program_type & (TRX_PROGRAM_TYPE_PROXY | TRX_PROGRAM_TYPE_SERVER)))
	{
		/* call the function DCget_psk_by_identity() by pointer */
		if (0 < find_psk_in_cache((const unsigned char *)identity, tls_psk_hex, &psk_usage))
		{
			/* The PSK is in configuration cache. Convert PSK to binary form. */
			if (0 >= (psk_bin_len = trx_psk_hex2bin(tls_psk_hex, psk_buf, sizeof(psk_buf))))
			{
				/* this should have been prevented by validation in frontend or API */
				treegix_log(LOG_LEVEL_WARNING, "cannot convert PSK to binary form for PSK identity"
						" \"%s\"", identity);
				goto fail;
			}

			psk_loc = (char *)psk_buf;
			psk_len = (size_t)psk_bin_len;
		}

		if (0 != (program_type & TRX_PROGRAM_TYPE_PROXY) &&
				0 < my_psk_identity_len &&
				0 == strcmp(my_psk_identity, identity))
		{
			/* the PSK is in proxy configuration file */
			psk_usage |= TRX_PSK_FOR_PROXY;

			if (0 < psk_len && (psk_len != my_psk_len || 0 != memcmp(psk_loc, my_psk, psk_len)))
			{
				/* PSK was also found in configuration cache but with different value */
				trx_psk_warn_misconfig(identity);
				psk_usage &= ~(unsigned int)TRX_PSK_FOR_AUTOREG;
			}

			psk_loc = my_psk;	/* prefer PSK from proxy configuration file */
			psk_len = my_psk_len;
		}

		if (0 == psk_len)
		{
			treegix_log(LOG_LEVEL_WARNING, "cannot find requested PSK identity \"%s\"", identity);
			goto fail;
		}
	}
	else if (0 != (program_type & TRX_PROGRAM_TYPE_AGENTD))
	{
		if (0 < my_psk_identity_len)
		{
			if (0 == strcmp(my_psk_identity, identity))
			{
				psk_loc = my_psk;
				psk_len = my_psk_len;
			}
			else
			{
				treegix_log(LOG_LEVEL_WARNING, "cannot find requested PSK identity \"%s\", available PSK"
						" identity \"%s\"", identity, my_psk_identity);
				goto fail;
			}
		}
	}

	if (0 < psk_len)
	{
		if ((size_t)max_psk_len < psk_len)
		{
			treegix_log(LOG_LEVEL_WARNING, "PSK associated with PSK identity \"%s\" does not fit into"
					" %u-byte buffer", identity, max_psk_len);
			goto fail;
		}

		memcpy(psk, psk_loc, psk_len);
		trx_strlcpy(incoming_connection_psk_id, identity, sizeof(incoming_connection_psk_id));

		return (unsigned int)psk_len;	/* success */
	}
fail:
	incoming_connection_psk_id[0] = '\0';
	return 0;	/* PSK not found */
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: trx_check_psk_identity_len                                       *
 *                                                                            *
 * Purpose: Check PSK identity length. Exit if length exceeds the maximum.    *
 *                                                                            *
 ******************************************************************************/
static void	trx_check_psk_identity_len(size_t psk_identity_len)
{
	if (HOST_TLS_PSK_IDENTITY_LEN < psk_identity_len)
	{
		treegix_log(LOG_LEVEL_CRIT, "PSK identity length " TRX_FS_SIZE_T " exceeds the maximum length of %d"
				" bytes.", (trx_fs_size_t)psk_identity_len, HOST_TLS_PSK_IDENTITY_LEN);
		trx_tls_free();
		exit(EXIT_FAILURE);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: trx_read_psk_file                                                *
 *                                                                            *
 * Purpose:                                                                   *
 *     read a pre-shared key from a configured file and convert it from       *
 *     textual representation (ASCII hex digit string) to a binary            *
 *     representation (byte string)                                           *
 *                                                                            *
 * Comments:                                                                  *
 *     Maximum length of PSK hex-digit string is defined by HOST_TLS_PSK_LEN. *
 *     Currently it is 512 characters, which encodes a 2048-bit PSK and is    *
 *     supported by GnuTLS and OpenSSL libraries (compiled with default       *
 *     parameters). PolarSSL supports up to 256-bit PSK (compiled with        *
 *     default parameters). If the key is longer an error message             *
 *     "ssl_set_psk(): SSL - Bad input parameters to function" will be logged *
 *     at runtime.                                                            *
 *                                                                            *
 ******************************************************************************/
static void	trx_read_psk_file(void)
{
	FILE		*f;
	size_t		len;
	int		len_bin, ret = FAIL;
	char		buf[HOST_TLS_PSK_LEN_MAX + 2];	/* up to 512 bytes of hex-digits, maybe 1-2 bytes for '\n', */
							/* 1 byte for terminating '\0' */
	char		buf_bin[HOST_TLS_PSK_LEN / 2];	/* up to 256 bytes of binary PSK */

	if (NULL == (f = fopen(CONFIG_TLS_PSK_FILE, "r")))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot open file \"%s\": %s", CONFIG_TLS_PSK_FILE, trx_strerror(errno));
		goto out;
	}

	if (NULL == fgets(buf, (int)sizeof(buf), f))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot read from file \"%s\" or file empty", CONFIG_TLS_PSK_FILE);
		goto out;
	}

	buf[strcspn(buf, "\r\n")] = '\0';	/* discard newline at the end of string */

	if (0 == (len = strlen(buf)))
	{
		treegix_log(LOG_LEVEL_CRIT, "file \"%s\" is empty", CONFIG_TLS_PSK_FILE);
		goto out;
	}

	if (HOST_TLS_PSK_LEN_MIN > len)
	{
		treegix_log(LOG_LEVEL_CRIT, "PSK in file \"%s\" is too short. Minimum is %d hex-digits",
				CONFIG_TLS_PSK_FILE, HOST_TLS_PSK_LEN_MIN);
		goto out;
	}

	if (HOST_TLS_PSK_LEN < len)
	{
		treegix_log(LOG_LEVEL_CRIT, "PSK in file \"%s\" is too long. Maximum is %d hex-digits",
				CONFIG_TLS_PSK_FILE, HOST_TLS_PSK_LEN);
		goto out;
	}

	if (0 >= (len_bin = trx_psk_hex2bin((unsigned char *)buf, (unsigned char *)buf_bin, sizeof(buf_bin))))
	{
		treegix_log(LOG_LEVEL_CRIT, "invalid PSK in file \"%s\"", CONFIG_TLS_PSK_FILE);
		goto out;
	}

	my_psk_len = (size_t)len_bin;
	my_psk = trx_malloc(my_psk, my_psk_len);
	memcpy(my_psk, buf_bin, my_psk_len);

	ret = SUCCEED;
out:
	if (NULL != f && 0 != fclose(f))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot close file \"%s\": %s", CONFIG_TLS_PSK_FILE, trx_strerror(errno));
		ret = FAIL;
	}

	if (SUCCEED == ret)
		return;

	trx_tls_free();
	exit(EXIT_FAILURE);
}

#if defined(HAVE_POLARSSL)
/******************************************************************************
 *                                                                            *
 * Function: trx_log_ciphersuites                                             *
 *                                                                            *
 * Purpose: write names of enabled mbed TLS ciphersuites into Treegix log for  *
 *          debugging                                                         *
 *                                                                            *
 * Parameters:                                                                *
 *     title1     - [IN] name of the calling function                         *
 *     title2     - [IN] name of the group of ciphersuites                    *
 *     cipher_ids - [IN] list of ciphersuite ids, terminated by 0             *
 *                                                                            *
 ******************************************************************************/
static void	trx_log_ciphersuites(const char *title1, const char *title2, const int *cipher_ids)
{
	if (SUCCEED == TRX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
	{
		char		*msg = NULL;
		size_t		msg_alloc = 0, msg_offset = 0;
		const int	*p;

		trx_snprintf_alloc(&msg, &msg_alloc, &msg_offset, "%s() %s ciphersuites:", title1, title2);

		for (p = cipher_ids; 0 != *p; p++)
			trx_snprintf_alloc(&msg, &msg_alloc, &msg_offset, " %s", ssl_get_ciphersuite_name(*p));

		treegix_log(LOG_LEVEL_DEBUG, "%s", msg);
		trx_free(msg);
	}
}
#elif defined(HAVE_GNUTLS)
/******************************************************************************
 *                                                                            *
 * Function: trx_log_ciphersuites                                             *
 *                                                                            *
 * Purpose: write names of enabled GnuTLS ciphersuites into Treegix log for    *
 *          debugging                                                         *
 *                                                                            *
 * Parameters:                                                                *
 *     title1  - [IN] name of the calling function                            *
 *     title2  - [IN] name of the group of ciphersuites                       *
 *     ciphers - [IN] list of ciphersuites                                    *
 *                                                                            *
 ******************************************************************************/
static void	trx_log_ciphersuites(const char *title1, const char *title2, gnutls_priority_t ciphers)
{
	if (SUCCEED == TRX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
	{
		char		*msg = NULL;
		size_t		msg_alloc = 0, msg_offset = 0;
		int		res;
		unsigned int	idx = 0, sidx;
		const char	*name;

		trx_snprintf_alloc(&msg, &msg_alloc, &msg_offset, "%s() %s ciphersuites:", title1, title2);

		while (1)
		{
			if (GNUTLS_E_SUCCESS == (res = gnutls_priority_get_cipher_suite_index(ciphers, idx++, &sidx)))
			{
				if (NULL != (name = gnutls_cipher_suite_info(sidx, NULL, NULL, NULL, NULL, NULL)))
					trx_snprintf_alloc(&msg, &msg_alloc, &msg_offset, " %s", name);
			}
			else if (GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE == res)
			{
				break;
			}

			/* ignore the 3rd possibility GNUTLS_E_UNKNOWN_CIPHER_SUITE */
			/* (see "man gnutls_priority_get_cipher_suite_index") */
		}

		treegix_log(LOG_LEVEL_DEBUG, "%s", msg);
		trx_free(msg);
	}
}
#elif defined(HAVE_OPENSSL)
/******************************************************************************
 *                                                                            *
 * Function: trx_log_ciphersuites                                             *
 *                                                                            *
 * Purpose: write names of enabled OpenSSL ciphersuites into Treegix log for   *
 *          debugging                                                         *
 *                                                                            *
 * Parameters:                                                                *
 *     title1  - [IN] name of the calling function                            *
 *     title2  - [IN] name of the group of ciphersuites                       *
 *     ciphers - [IN] stack of ciphersuites                                   *
 *                                                                            *
 ******************************************************************************/
static void	trx_log_ciphersuites(const char *title1, const char *title2, SSL_CTX *ciphers)
{
	if (SUCCEED == TRX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
	{
		char			*msg = NULL;
		size_t			msg_alloc = 0, msg_offset = 0;
		int			i, num;
		STACK_OF(SSL_CIPHER)	*cipher_list;

		trx_snprintf_alloc(&msg, &msg_alloc, &msg_offset, "%s() %s ciphersuites:", title1, title2);

		cipher_list = SSL_CTX_get_ciphers(ciphers);
		num = sk_SSL_CIPHER_num(cipher_list);

		for (i = 0; i < num; i++)
		{
			trx_snprintf_alloc(&msg, &msg_alloc, &msg_offset, " %s",
					SSL_CIPHER_get_name(sk_SSL_CIPHER_value(cipher_list, i)));
		}

		treegix_log(LOG_LEVEL_DEBUG, "%s", msg);
		trx_free(msg);
	}
}
#endif

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS)
/******************************************************************************
 *                                                                            *
 * Function: trx_print_rdn_value                                              *
 *                                                                            *
 * Purpose:                                                                   *
 *     print an RDN (relative distinguished name) value into buffer           *
 *                                                                            *
 * Parameters:                                                                *
 *     value - [IN] pointer to RDN value                                      *
 *     len   - [IN] number of bytes in the RDN value                          *
 *     buf   - [OUT] output buffer                                            *
 *     size  - [IN] output buffer size                                        *
 *     error - [OUT] dynamically allocated memory with error message.         *
 *                   Initially '*error' must be NULL.                         *
 *                                                                            *
 * Return value:                                                              *
 *     number of bytes written into 'buf'                                     *
 *     '*error' is not NULL if an error occurred                              *
 *                                                                            *
 ******************************************************************************/
static size_t	trx_print_rdn_value(const unsigned char *value, size_t len, unsigned char *buf, size_t size,
		char **error)
{
	const unsigned char	*p_in;
	unsigned char		*p_out, *p_out_end;

	p_in = value;
	p_out = buf;
	p_out_end = buf + size;

	while (value + len > p_in)
	{
		if (0 == (*p_in & 0x80))			/* ASCII */
		{
			if (0x1f < *p_in && *p_in < 0x7f)	/* printable character */
			{
				if (p_out_end - 1 > p_out)
				{
					/* According to RFC 4514:                                                   */
					/*    - escape characters '"' (U+0022), '+' U+002B, ',' U+002C, ';' U+003B, */
					/*      '<' U+003C, '>' U+003E, '\' U+005C  anywhere in string.             */
					/*    - escape characters space (' ' U+0020) or number sign ('#' U+0023) at */
					/*      the beginning of string.                                            */
					/*    - escape character space (' ' U+0020) at the end of string.           */
					/*    - escape null (U+0000) character anywhere. <--- we do not allow null. */

					if ((0x20 == (*p_in & 0x70) && ('"' == *p_in || '+' == *p_in || ',' == *p_in))
							|| (0x30 == (*p_in & 0x70) && (';' == *p_in || '<' == *p_in ||
							'>' == *p_in)) || '\\' == *p_in ||
							(' ' == *p_in && (value == p_in || (value + len - 1) == p_in))
							|| ('#' == *p_in && value == p_in))
					{
						*p_out++ = '\\';
					}
				}
				else
					goto small_buf;

				if (p_out_end - 1 > p_out)
					*p_out++ = *p_in++;
				else
					goto small_buf;
			}
			else if (0 == *p_in)
			{
				*error = trx_strdup(*error, "null byte in certificate, could be an attack");
				break;
			}
			else
			{
				*error = trx_strdup(*error, "non-printable character in certificate");
				break;
			}
		}
		else if (0xc0 == (*p_in & 0xe0))	/* 11000010-11011111 starts a 2-byte sequence */
		{
			if (p_out_end - 2 > p_out)
			{
				*p_out++ = *p_in++;
				*p_out++ = *p_in++;
			}
			else
				goto small_buf;
		}
		else if (0xe0 == (*p_in & 0xf0))	/* 11100000-11101111 starts a 3-byte sequence */
		{
			if (p_out_end - 3 > p_out)
			{
				*p_out++ = *p_in++;
				*p_out++ = *p_in++;
				*p_out++ = *p_in++;
			}
			else
				goto small_buf;
		}
		else if (0xf0 == (*p_in & 0xf8))	/* 11110000-11110100 starts a 4-byte sequence */
		{
			if (p_out_end - 4 > p_out)
			{
				*p_out++ = *p_in++;
				*p_out++ = *p_in++;
				*p_out++ = *p_in++;
				*p_out++ = *p_in++;
			}
			else
				goto small_buf;
		}
		else				/* not a valid UTF-8 character */
		{
			*error = trx_strdup(*error, "invalid UTF-8 character");
			break;
		}
	}

	*p_out = '\0';

	return (size_t)(p_out - buf);
small_buf:
	*p_out = '\0';
	*error = trx_strdup(*error, "output buffer too small");

	return (size_t)(p_out - buf);
}
#endif

#if defined(HAVE_POLARSSL)
/******************************************************************************
 *                                                                            *
 * Function: trx_x509_dn_gets                                                 *
 *                                                                            *
 * Purpose:                                                                   *
 *     Print distinguished name (i.e. issuer, subject) into buffer. Intended  *
 *     to use as an alternative to PolarSSL x509_dn_gets() to meet            *
 *     application needs.                                                     *
 *                                                                            *
 * Parameters:                                                                *
 *     dn    - [IN] pointer to distinguished name                             *
 *     buf   - [OUT] output buffer                                            *
 *     size  - [IN] output buffer size                                        *
 *     error - [OUT] dynamically allocated memory with error message          *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - no errors, FAIL - an error occurred                          *
 *                                                                            *
 * Comments:                                                                  *
 *     This function is derived from PolarSSL x509_dn_gets() and heavily      *
 *     modified to print RDNs in reverse order, to print UTF-8 characters and *
 *     non-printable characters in a different way than original              *
 *     x509_dn_gets() does and to return error messages.                      *
 *     Multi-valued RDNs are not supported currently.                         *
 *                                                                            *
 ******************************************************************************/
static int	trx_x509_dn_gets(const x509_name *dn, char *buf, size_t size, char **error)
{
	const x509_name	*node, *stop_node = NULL;
	const char	*short_name = NULL;
	char		*p, *p_end;

	/* We need to traverse a linked list of RDNs and print them out in reverse order (recommended by RFC 4514).   */
	/* The number of RDNs in DN is expected to be small (typically 4-5, sometimes up to 8). For such a small list */
	/* we simply traverse it multiple times for getting elements in reverse order. */

	p = buf;
	p_end = buf + size;

	while (1)
	{
		node = dn;

		while (stop_node != node->next)
			node = node->next;

		if (NULL != node->oid.p)
		{
			if (buf != p)				/* not the first RDN */
			{
				if (p_end - 1 == p)
					goto small_buf;

				p += trx_strlcpy(p, ",", (size_t)(p_end - p));	/* separator between RDNs */
			}

			/* write attribute name */

			if (0 == oid_get_attr_short_name(&node->oid, &short_name))
			{
				if (p_end - 1 == p)
					goto small_buf;

				p += trx_strlcpy(p, short_name, (size_t)(p_end - p));
			}
			else	/* unknown OID name, write in numerical form */
			{
				int	res;

				if (p_end - 1 == p)
					goto small_buf;

				if (0 < (res = oid_get_numeric_string(p, (size_t)(p_end - p), &node->oid)))
					p += (size_t)res;
				else
					goto small_buf;
			}

			if (p_end - 1 == p)
				goto small_buf;

			p += trx_strlcpy(p, "=", (size_t)(p_end - p));

			/* write attribute value */

			if (p_end - 1 == p)
				goto small_buf;

			p += trx_print_rdn_value(node->val.p, node->val.len, (unsigned char *)p, (size_t)(p_end - p),
					error);

			if (NULL != *error)
				break;
		}

		if (dn->next != stop_node)
			stop_node = node;
		else
			break;
	}

	if (NULL == *error)
		return SUCCEED;
	else
		return FAIL;
small_buf:
	*error = trx_strdup(*error, "output buffer too small");

	return FAIL;
}
#elif defined(HAVE_GNUTLS)
/******************************************************************************
 *                                                                            *
 * Function: trx_x509_dn_gets                                                 *
 *                                                                            *
 * Purpose:                                                                   *
 *     Print distinguished name (i.e. issuer, subject) into buffer. Intended  *
 *     to use as an alternative to GnuTLS gnutls_x509_crt_get_issuer_dn() and *
 *     gnutls_x509_crt_get_dn() to meet application needs.                    *
 *                                                                            *
 * Parameters:                                                                *
 *     dn    - [IN] pointer to distinguished name                             *
 *     buf   - [OUT] output buffer                                            *
 *     size  - [IN] output buffer size                                        *
 *     error - [OUT] dynamically allocated memory with error message          *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - no errors, FAIL - an error occurred                          *
 *                                                                            *
 * Comments:                                                                  *
 *     Multi-valued RDNs are not supported currently (only the first value is *
 *     printed).                                                              *
 *                                                                            *
 ******************************************************************************/
static int	trx_x509_dn_gets(const gnutls_x509_dn_t dn, char *buf, size_t size, char **error)
{
#define TRX_AVA_BUF_SIZE	20	/* hopefully no more than 20 RDNs */

	int			res, i = 0, i_max, ava_dyn_size;
	char			*p, *p_end;
	gnutls_x509_ava_st	*ava, *ava_dyn = NULL;
	char			oid_str[128];		/* size equal to MAX_OID_SIZE, internally defined in GnuTLS */
	gnutls_x509_ava_st	ava_stat[TRX_AVA_BUF_SIZE];

	/* Find index of the last RDN in distinguished name. Remember pointers to RDNs to minimize calling of */
	/* gnutls_x509_dn_get_rdn_ava() as it seems a bit expensive. */

	while (1)
	{
		if (TRX_AVA_BUF_SIZE > i)	/* most common case: small number of RDNs, fits in fixed buffer */
		{
			ava = &ava_stat[i];
		}
		else if (NULL == ava_dyn)	/* fixed buffer full, copy data to dynamic buffer */
		{
			ava_dyn_size = 2 * TRX_AVA_BUF_SIZE;
			ava_dyn = trx_malloc(NULL, (size_t)ava_dyn_size * sizeof(gnutls_x509_ava_st));

			memcpy(ava_dyn, ava_stat, TRX_AVA_BUF_SIZE * sizeof(gnutls_x509_ava_st));
			ava = ava_dyn + TRX_AVA_BUF_SIZE;
		}
		else if (ava_dyn_size > i)	/* fits in dynamic buffer */
		{
			ava = ava_dyn + i;
		}
		else				/* expand dynamic buffer */
		{
			ava_dyn_size += TRX_AVA_BUF_SIZE;
			ava_dyn = trx_realloc(ava_dyn, (size_t)ava_dyn_size * sizeof(gnutls_x509_ava_st));
			ava = ava_dyn + i;
		}

		if (0 == (res = gnutls_x509_dn_get_rdn_ava(dn, i, 0, ava)))	/* RDN with index 'i' exists */
		{
			i++;
		}
		else if (GNUTLS_E_ASN1_ELEMENT_NOT_FOUND == res)
		{
			i_max = i;
			break;
		}
		else
		{
			*error = trx_dsprintf(*error, "trx_x509_dn_gets(): gnutls_x509_dn_get_rdn_ava() failed: %d %s",
					res, gnutls_strerror(res));
			trx_free(ava_dyn);
			return FAIL;
		}
	}

	/* "print" RDNs in reverse order (recommended by RFC 4514) */

	if (NULL == ava_dyn)
		ava = &ava_stat[0];
	else
		ava = ava_dyn;

	p = buf;
	p_end = buf + size;

	for (i = i_max - 1, ava += i; i >= 0; i--, ava--)
	{
		if (sizeof(oid_str) <= ava->oid.size)
		{
			THIS_SHOULD_NEVER_HAPPEN;
			trx_free(ava_dyn);
			return FAIL;
		}

		memcpy(oid_str, ava->oid.data, ava->oid.size);
		oid_str[ava->oid.size] = '\0';

		if (buf != p)			/* not the first RDN being printed */
		{
			if (p_end - 1 == p)
				goto small_buf;

			p += trx_strlcpy(p, ",", (size_t)(p_end - p));	/* separator between RDNs */
		}

		/* write attribute name */

		if (p_end - 1 == p)
			goto small_buf;

		p += trx_strlcpy(p, gnutls_x509_dn_oid_name(oid_str, GNUTLS_X509_DN_OID_RETURN_OID),
				(size_t)(p_end - p));

		if (p_end - 1 == p)
			goto small_buf;

		p += trx_strlcpy(p, "=", (size_t)(p_end - p));

		/* write attribute value */

		if (p_end - 1 == p)
			goto small_buf;

		p += trx_print_rdn_value(ava->value.data, ava->value.size, (unsigned char *)p, (size_t)(p_end - p),
				error);

		if (NULL != *error)
			break;
	}

	trx_free(ava_dyn);

	if (NULL == *error)
		return SUCCEED;
	else
		return FAIL;
small_buf:
	trx_free(ava_dyn);
	*error = trx_strdup(*error, "output buffer too small");

	return FAIL;

#undef TRX_AVA_BUF_SIZE
}
#elif defined(HAVE_OPENSSL)
/******************************************************************************
 *                                                                            *
 * Function: trx_x509_dn_gets                                                 *
 *                                                                            *
 * Purpose:                                                                   *
 *     Print distinguished name (i.e. issuer, subject) into buffer. Intended  *
 *     to use as an alternative to OpenSSL X509_NAME_oneline() and to meet    *
 *     application needs.                                                     *
 *                                                                            *
 * Parameters:                                                                *
 *     dn    - [IN] pointer to distinguished name                             *
 *     buf   - [OUT] output buffer                                            *
 *     size  - [IN] output buffer size                                        *
 *     error - [OUT] dynamically allocated memory with error message          *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - no errors, FAIL - an error occurred                          *
 *                                                                            *
 * Comments:                                                                  *
 *     Examples often use OpenSSL X509_NAME_oneline() to print certificate    *
 *     issuer and subject into memory buffers but it is a legacy function and *
 *     strongly discouraged in new applications. So, we have to use functions *
 *     writing into BIOs and then turn results into memory buffers.           *
 *                                                                            *
 ******************************************************************************/
static int	trx_x509_dn_gets(X509_NAME *dn, char *buf, size_t size, char **error)
{
	BIO		*bio;
	const char	*data;
	size_t		len;
	int		ret = FAIL;

	if (NULL == (bio = BIO_new(BIO_s_mem())))
	{
		*error = trx_strdup(*error, "cannot create BIO");
		goto out;
	}

	/* XN_FLAG_RFC2253 - RFC 2253 is outdated, it was replaced by RFC 4514 "Lightweight Directory Access Protocol */
	/* (LDAP): String Representation of Distinguished Names" */

	if (0 > X509_NAME_print_ex(bio, dn, 0, XN_FLAG_RFC2253 & ~ASN1_STRFLGS_ESC_MSB))
	{
		*error = trx_strdup(*error, "cannot print distinguished name");
		goto out;
	}

	if (size <= (len = (size_t)BIO_get_mem_data(bio, &data)))
	{
		*error = trx_strdup(*error, "output buffer too small");
		goto out;
	}

	trx_strlcpy(buf, data, len + 1);
	ret = SUCCEED;
out:
	if (NULL != bio)
	{
		/* ensure that associated memory buffer will be freed by BIO_vfree() */
		(void)BIO_set_close(bio, BIO_CLOSE);
		BIO_vfree(bio);
	}

	return ret;
}
#endif

#if defined(HAVE_GNUTLS)
/******************************************************************************
 *                                                                            *
 * Function: trx_get_peer_cert                                                *
 *                                                                            *
 * Purpose: get peer certificate from session                                 *
 *                                                                            *
 * Parameters:                                                                *
 *     session - [IN] session context                                         *
 *     error   - [OUT] dynamically allocated memory with error message        *
 *                                                                            *
 * Return value:                                                              *
 *     pointer to peer certificate - success                                  *
 *     NULL - an error occurred                                               *
 *                                                                            *
 * Comments:                                                                  *
 *     In case of success it is a responsibility of caller to deallocate      *
 *     the instance of certificate using gnutls_x509_crt_deinit().            *
 *                                                                            *
 ******************************************************************************/
static gnutls_x509_crt_t	trx_get_peer_cert(const gnutls_session_t session, char **error)
{
	if (GNUTLS_CRT_X509 == gnutls_certificate_type_get(session))
	{
		int			res;
		unsigned int		cert_list_size = 0;
		const gnutls_datum_t	*cert_list;
		gnutls_x509_crt_t	cert;

		if (NULL == (cert_list = gnutls_certificate_get_peers(session, &cert_list_size)))
		{
			*error = trx_dsprintf(*error, "%s(): gnutls_certificate_get_peers() returned NULL", __func__);
			return NULL;
		}

		if (0 == cert_list_size)
		{
			*error = trx_dsprintf(*error, "%s(): gnutls_certificate_get_peers() returned 0 certificates",
					__func__);
			return NULL;
		}

		if (GNUTLS_E_SUCCESS != (res = gnutls_x509_crt_init(&cert)))
		{
			*error = trx_dsprintf(*error, "%s(): gnutls_x509_crt_init() failed: %d %s", __func__,
					res, gnutls_strerror(res));
			return NULL;
		}

		/* the 1st element of the list is peer certificate */

		if (GNUTLS_E_SUCCESS != (res = gnutls_x509_crt_import(cert, &cert_list[0], GNUTLS_X509_FMT_DER)))
		{
			*error = trx_dsprintf(*error, "%s(): gnutls_x509_crt_import() failed: %d %s", __func__,
					res, gnutls_strerror(res));
			gnutls_x509_crt_deinit(cert);
			return NULL;
		}

		return cert;	/* success */
	}
	else
	{
		*error = trx_dsprintf(*error, "%s(): not an X509 certificate", __func__);
		return NULL;
	}
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: trx_log_peer_cert                                                *
 *                                                                            *
 * Purpose: write peer certificate information into Treegix log for debugging  *
 *                                                                            *
 * Parameters:                                                                *
 *     function_name - [IN] caller function name                              *
 *     tls_ctx       - [IN] TLS context                                       *
 *                                                                            *
 ******************************************************************************/
static void	trx_log_peer_cert(const char *function_name, const trx_tls_context_t *tls_ctx)
{
	char			*error = NULL;
#if defined(HAVE_POLARSSL)
	const x509_crt		*cert;
	char			issuer[HOST_TLS_ISSUER_LEN_MAX], subject[HOST_TLS_SUBJECT_LEN_MAX], serial[128];
#elif defined(HAVE_GNUTLS)
	gnutls_x509_crt_t	cert;
	int			res;
	gnutls_datum_t		cert_print;
#elif defined(HAVE_OPENSSL)
	X509			*cert;
	char			issuer[HOST_TLS_ISSUER_LEN_MAX], subject[HOST_TLS_SUBJECT_LEN_MAX];
#endif

	if (SUCCEED != TRX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
		return;
#if defined(HAVE_POLARSSL)
	if (NULL == (cert = ssl_get_peer_cert(tls_ctx->ctx)))
	{
		treegix_log(LOG_LEVEL_DEBUG, "%s() cannot obtain peer certificate", function_name);
	}
	else if (SUCCEED != trx_x509_dn_gets(&cert->issuer, issuer, sizeof(issuer), &error))
	{
		treegix_log(LOG_LEVEL_DEBUG, "%s() cannot obtain peer certificate issuer: %s", function_name, error);
	}
	else if (SUCCEED != trx_x509_dn_gets(&cert->subject, subject, sizeof(subject), &error))
	{
		treegix_log(LOG_LEVEL_DEBUG, "%s() cannot obtain peer certificate subject: %s", function_name, error);
	}
	else if (0 > x509_serial_gets(serial, sizeof(serial), &cert->serial))
	{
		treegix_log(LOG_LEVEL_DEBUG, "%s() cannot obtain peer certificate serial", function_name);
	}
	else
	{
		treegix_log(LOG_LEVEL_DEBUG, "%s() peer certificate issuer:\"%s\" subject:\"%s\" serial:\"%s\"",
				function_name, issuer, subject, serial);
	}
#elif defined(HAVE_GNUTLS)
	if (NULL == (cert = trx_get_peer_cert(tls_ctx->ctx, &error)))
	{
		treegix_log(LOG_LEVEL_DEBUG, "%s(): cannot obtain peer certificate: %s", function_name, error);
	}
	else if (GNUTLS_E_SUCCESS != (res = gnutls_x509_crt_print(cert, GNUTLS_CRT_PRINT_ONELINE, &cert_print)))
	{
		treegix_log(LOG_LEVEL_DEBUG, "%s(): gnutls_x509_crt_print() failed: %d %s", function_name, res,
				gnutls_strerror(res));
	}
	else
	{
		treegix_log(LOG_LEVEL_DEBUG, "%s(): peer certificate: %s", function_name, cert_print.data);
		gnutls_free(cert_print.data);
	}

	if (NULL != cert)
		gnutls_x509_crt_deinit(cert);
#elif defined(HAVE_OPENSSL)
	if (NULL == (cert = SSL_get_peer_certificate(tls_ctx->ctx)))
	{
		treegix_log(LOG_LEVEL_DEBUG, "%s() cannot obtain peer certificate", function_name);
	}
	else if (SUCCEED != trx_x509_dn_gets(X509_get_issuer_name(cert), issuer, sizeof(issuer), &error))
	{
		treegix_log(LOG_LEVEL_DEBUG, "%s() cannot obtain peer certificate issuer: %s", function_name, error);
	}
	else if (SUCCEED != trx_x509_dn_gets(X509_get_subject_name(cert), subject, sizeof(subject), &error))
	{
		treegix_log(LOG_LEVEL_DEBUG, "%s() cannot obtain peer certificate subject: %s", function_name, error);
	}
	else
	{
		treegix_log(LOG_LEVEL_DEBUG, "%s() peer certificate issuer:\"%s\" subject:\"%s\"",
				function_name, issuer, subject);
	}

	if (NULL != cert)
		X509_free(cert);
#endif
	trx_free(error);
}

#if defined(HAVE_GNUTLS)
/******************************************************************************
 *                                                                            *
 * Function: trx_verify_peer_cert                                             *
 *                                                                            *
 * Purpose: basic verification of peer certificate                            *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - verification successful                                      *
 *     FAIL - invalid certificate or an error occurred                        *
 *                                                                            *
 ******************************************************************************/
static int	trx_verify_peer_cert(const gnutls_session_t session, char **error)
{
	int		res;
	unsigned int	status;
	gnutls_datum_t	status_print;

	if (GNUTLS_E_SUCCESS != (res = gnutls_certificate_verify_peers2(session, &status)))
	{
		*error = trx_dsprintf(*error, "%s(): gnutls_certificate_verify_peers2() failed: %d %s",
				__func__, res, gnutls_strerror(res));
		return FAIL;
	}

	if (GNUTLS_E_SUCCESS != (res = gnutls_certificate_verification_status_print(status,
			gnutls_certificate_type_get(session), &status_print, 0)))
	{
		*error = trx_dsprintf(*error, "%s(): gnutls_certificate_verification_status_print() failed: %d"
				" %s", __func__, res, gnutls_strerror(res));
		return FAIL;
	}

	if (0 != status)
		*error = trx_dsprintf(*error, "invalid peer certificate: %s", status_print.data);

	gnutls_free(status_print.data);

	if (0 == status)
		return SUCCEED;
	else
		return FAIL;
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: trx_verify_issuer_subject                                        *
 *                                                                            *
 * Purpose:                                                                   *
 *     verify peer certificate issuer and subject of the given TLS context    *
 *                                                                            *
 * Parameters:                                                                *
 *     tls_ctx      - [IN] TLS context to verify                              *
 *     issuer       - [IN] required issuer (ignore if NULL or empty string)   *
 *     subject      - [IN] required subject (ignore if NULL or empty string)  *
 *     error        - [OUT] dynamically allocated memory with error message   *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED or FAIL                                                        *
 *                                                                            *
 ******************************************************************************/
static int	trx_verify_issuer_subject(const trx_tls_context_t *tls_ctx, const char *issuer, const char *subject,
		char **error)
{
	char			tls_issuer[HOST_TLS_ISSUER_LEN_MAX], tls_subject[HOST_TLS_SUBJECT_LEN_MAX];
	int			issuer_mismatch = 0, subject_mismatch = 0;
	size_t			error_alloc = 0, error_offset = 0;
#if defined(HAVE_POLARSSL)
	const x509_crt		*cert;
#elif defined(HAVE_GNUTLS)
	gnutls_x509_crt_t	cert;
	gnutls_x509_dn_t	dn;
	int			res;
#elif defined(HAVE_OPENSSL)
	X509			*cert;
#endif

	if ((NULL == issuer || '\0' == *issuer) && (NULL == subject || '\0' == *subject))
		return SUCCEED;

	tls_issuer[0] = '\0';
	tls_subject[0] = '\0';

#if defined(HAVE_POLARSSL)
	if (NULL == (cert = ssl_get_peer_cert(tls_ctx->ctx)))
	{
		*error = trx_strdup(*error, "cannot obtain peer certificate");
		return FAIL;
	}

	if (NULL != issuer && '\0' != *issuer)
	{
		if (SUCCEED != trx_x509_dn_gets(&cert->issuer, tls_issuer, sizeof(tls_issuer), error))
			return FAIL;
	}

	if (NULL != subject && '\0' != *subject)
	{
		if (SUCCEED != trx_x509_dn_gets(&cert->subject, tls_subject, sizeof(tls_subject), error))
			return FAIL;
	}
#elif defined(HAVE_GNUTLS)
	if (NULL == (cert = trx_get_peer_cert(tls_ctx->ctx, error)))
		return FAIL;

	if (NULL != issuer && '\0' != *issuer)
	{
		if (0 != (res = gnutls_x509_crt_get_issuer(cert, &dn)))
		{
			*error = trx_dsprintf(*error, "gnutls_x509_crt_get_issuer() failed: %d %s", res,
					gnutls_strerror(res));
			return FAIL;
		}

		if (SUCCEED != trx_x509_dn_gets(dn, tls_issuer, sizeof(tls_issuer), error))
			return FAIL;
	}

	if (NULL != subject && '\0' != *subject)
	{
		if (0 != (res = gnutls_x509_crt_get_subject(cert, &dn)))
		{
			*error = trx_dsprintf(*error, "gnutls_x509_crt_get_subject() failed: %d %s", res,
					gnutls_strerror(res));
			return FAIL;
		}

		if (SUCCEED != trx_x509_dn_gets(dn, tls_subject, sizeof(tls_subject), error))
			return FAIL;
	}

	gnutls_x509_crt_deinit(cert);
#elif defined(HAVE_OPENSSL)
	if (NULL == (cert = SSL_get_peer_certificate(tls_ctx->ctx)))
	{
		*error = trx_strdup(*error, "cannot obtain peer certificate");
		return FAIL;
	}

	if (NULL != issuer && '\0' != *issuer)
	{
		if (SUCCEED != trx_x509_dn_gets(X509_get_issuer_name(cert), tls_issuer, sizeof(tls_issuer), error))
			return FAIL;
	}

	if (NULL != subject && '\0' != *subject)
	{
		if (SUCCEED != trx_x509_dn_gets(X509_get_subject_name(cert), tls_subject, sizeof(tls_subject), error))
			return FAIL;
	}

	X509_free(cert);
#endif
	/* simplified match, not compliant with RFC 4517, 4518 */

	if (NULL != issuer && '\0' != *issuer)
		issuer_mismatch = strcmp(tls_issuer, issuer);

	if (NULL != subject && '\0' != *subject)
		subject_mismatch = strcmp(tls_subject, subject);

	if (0 == issuer_mismatch && 0 == subject_mismatch)
		return SUCCEED;

	if (0 != issuer_mismatch)
	{
		trx_snprintf_alloc(error, &error_alloc, &error_offset, "issuer: peer: \"%s\", required: \"%s\"",
				tls_issuer, issuer);
	}

	if (0 != subject_mismatch)
	{
		if (0 != issuer_mismatch)
			trx_strcpy_alloc(error, &error_alloc, &error_offset, ", ");

		trx_snprintf_alloc(error, &error_alloc, &error_offset, "subject: peer: \"%s\", required: \"%s\"",
				tls_subject, subject);
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_check_server_issuer_subject                                  *
 *                                                                            *
 * Purpose:                                                                   *
 *     check server certificate issuer and subject (for passive proxies and   *
 *     agent passive checks)                                                  *
 *                                                                            *
 * Parameters:                                                                *
 *     sock  - [IN] certificate to verify                                     *
 *     error - [OUT] dynamically allocated memory with error message          *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED or FAIL                                                        *
 *                                                                            *
 ******************************************************************************/
int	trx_check_server_issuer_subject(trx_socket_t *sock, char **error)
{
	trx_tls_conn_attr_t	attr;

	if (SUCCEED != trx_tls_get_attr_cert(sock, &attr))
	{
		THIS_SHOULD_NEVER_HAPPEN;

		*error = trx_dsprintf(*error, "cannot get connection attributes for connection from %s", sock->peer);
		return FAIL;
	}

	/* simplified match, not compliant with RFC 4517, 4518 */
	if (NULL != CONFIG_TLS_SERVER_CERT_ISSUER && 0 != strcmp(CONFIG_TLS_SERVER_CERT_ISSUER, attr.issuer))
	{
		*error = trx_dsprintf(*error, "certificate issuer does not match for %s", sock->peer);
		return FAIL;
	}

	/* simplified match, not compliant with RFC 4517, 4518 */
	if (NULL != CONFIG_TLS_SERVER_CERT_SUBJECT && 0 != strcmp(CONFIG_TLS_SERVER_CERT_SUBJECT, attr.subject))
	{
		*error = trx_dsprintf(*error, "certificate subject does not match for %s", sock->peer);
		return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tls_library_init                                             *
 *                                                                            *
 * Purpose: initialize TLS library, log library version                       *
 *                                                                            *
 * Comments:                                                                  *
 *     Some crypto libraries require initialization. On Unix the              *
 *     initialization is done separately in each child process which uses     *
 *     crypto libraries. On MS Windows it is done in the first thread.        *
 *                                                                            *
 *     Flag 'init_done' is used to prevent library deinitialzation on exit if *
 *     it was not yet initialized (can happen if termination signal is        *
 *     received).                                                             *
 *                                                                            *
 ******************************************************************************/
static void	trx_tls_library_init(void)
{
#if defined(HAVE_POLARSSL)
	treegix_log(LOG_LEVEL_DEBUG, "mbed TLS library (version %s)", POLARSSL_VERSION_STRING_FULL);
#elif defined(HAVE_GNUTLS)
	if (GNUTLS_E_SUCCESS != gnutls_global_init())
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot initialize GnuTLS library");
		exit(EXIT_FAILURE);
	}

	init_done = 1;

	treegix_log(LOG_LEVEL_DEBUG, "GnuTLS library (version %s) initialized", gnutls_check_version(NULL));
#elif defined(HAVE_OPENSSL)
	if (1 != trx_openssl_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot initialize OpenSSL library");
		exit(EXIT_FAILURE);
	}

	init_done = 1;

	treegix_log(LOG_LEVEL_DEBUG, "OpenSSL library (version %s) initialized", OpenSSL_version(OPENSSL_VERSION));
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tls_library_deinit                                           *
 *                                                                            *
 * Purpose: deinitialize TLS library                                          *
 *                                                                            *
 ******************************************************************************/
void	trx_tls_library_deinit(void)
{
#if defined(HAVE_GNUTLS)
	if (1 == init_done)
	{
		init_done = 0;
		gnutls_global_deinit();
	}
#elif defined(HAVE_OPENSSL)
	if (1 == init_done)
	{
		init_done = 0;
		OPENSSL_cleanup();
	}
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tls_init_parent                                              *
 *                                                                            *
 * Purpose: initialize TLS library in a parent process                        *
 *                                                                            *
 ******************************************************************************/
void	trx_tls_init_parent(void)
{
#if defined(_WINDOWS)
	trx_tls_library_init();		/* on MS Windows initialize crypto libraries in parent thread */
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tls_init_child                                               *
 *                                                                            *
 * Purpose: read available configuration parameters and initialize TLS        *
 *          library in a child process                                        *
 *                                                                            *
 ******************************************************************************/
#if defined(HAVE_POLARSSL)
void	trx_tls_init_child(void)
{
	int		res;
	unsigned char	pers[64];	/* personalization string obtained from SHA-512 in SHA-384 mode */
#ifndef _WINDOWS
	sigset_t	mask, orig_mask;
#endif
	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

#ifndef _WINDOWS
	/* Invalid TLS parameters will cause exit. Once one process exits the parent process will send SIGHUP to */
	/* child processes which may be on their way to exit on their own - do not interrupt them, block signal */
	/* SIGHUP and unblock it when TLS parameters are good and libraries are initialized. */
	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGHUP);
	sigaddset(&mask, SIGUSR2);
	sigaddset(&mask, SIGQUIT);
	sigprocmask(SIG_BLOCK, &mask, &orig_mask);

	trx_tls_library_init();		/* on Unix initialize crypto libraries in child processes */
#endif
	/* 'TLSCAFile' parameter (in treegix_server.conf, treegix_proxy.conf, treegix_agentd.conf). */
	if (NULL != CONFIG_TLS_CA_FILE)
	{
		ca_cert = trx_malloc(ca_cert, sizeof(x509_crt));
		x509_crt_init(ca_cert);

		if (0 != (res = x509_crt_parse_file(ca_cert, CONFIG_TLS_CA_FILE)))
		{
			if (0 > res)
			{
				trx_tls_error_msg(res, "", &err_msg);
				treegix_log(LOG_LEVEL_CRIT, "cannot parse CA certificate(s) in file \"%s\": %s",
						CONFIG_TLS_CA_FILE, err_msg);
				trx_free(err_msg);
			}
			else
			{
				treegix_log(LOG_LEVEL_CRIT, "cannot parse %d CA certificate(s) in file \"%s\"", res,
						CONFIG_TLS_CA_FILE);
			}

			trx_tls_free();
			exit(EXIT_FAILURE);
		}

		treegix_log(LOG_LEVEL_DEBUG, "%s() loaded CA certificate(s) from file \"%s\"", __func__,
				CONFIG_TLS_CA_FILE);
	}

	/* 'TLSCRLFile' parameter (in treegix_server.conf, treegix_proxy.conf, treegix_agentd.conf). */
	/* Load CRL (certificate revocation list) file. */
	if (NULL != CONFIG_TLS_CRL_FILE)
	{
		crl = trx_malloc(crl, sizeof(x509_crl));
		x509_crl_init(crl);

		if (0 != (res = x509_crl_parse_file(crl, CONFIG_TLS_CRL_FILE)))
		{
			trx_tls_error_msg(res, "", &err_msg);
			treegix_log(LOG_LEVEL_CRIT, "cannot parse CRL file \"%s\": %s", CONFIG_TLS_CRL_FILE, err_msg);
			trx_free(err_msg);

			trx_tls_free();
			exit(EXIT_FAILURE);
		}

		treegix_log(LOG_LEVEL_DEBUG, "%s() loaded CRL(s) from file \"%s\"", __func__,
				CONFIG_TLS_CRL_FILE);
	}

	/* 'TLSCertFile' parameter (in treegix_server.conf, treegix_proxy.conf, treegix_agentd.conf). */
	/* Load certificate. */
	if (NULL != CONFIG_TLS_CERT_FILE)
	{
		my_cert = trx_malloc(my_cert, sizeof(x509_crt));
		x509_crt_init(my_cert);

		if (0 != (res = x509_crt_parse_file(my_cert, CONFIG_TLS_CERT_FILE)))
		{
			if (0 > res)
			{
				trx_tls_error_msg(res, "", &err_msg);
				treegix_log(LOG_LEVEL_CRIT, "cannot parse certificate(s) in file \"%s\": %s",
						CONFIG_TLS_CERT_FILE, err_msg);
				trx_free(err_msg);
			}
			else
			{
				treegix_log(LOG_LEVEL_CRIT, "cannot parse %d certificate(s) in file \"%s\"", res,
						CONFIG_TLS_CERT_FILE);
			}

			trx_tls_free();
			exit(EXIT_FAILURE);
		}

		treegix_log(LOG_LEVEL_DEBUG, "%s() loaded certificate from file \"%s\"", __func__, CONFIG_TLS_CERT_FILE);
	}

	/* 'TLSKeyFile' parameter (in treegix_server.conf, treegix_proxy.conf, treegix_agentd.conf). */
	/* Load private key. */
	if (NULL != CONFIG_TLS_KEY_FILE)
	{
		my_priv_key = trx_malloc(my_priv_key, sizeof(pk_context));
		pk_init(my_priv_key);

		/* The 3rd argument of pk_parse_keyfile() is password for decrypting the private key. */
		/* Currently the password is not used, it is empty. */
		if (0 != (res = pk_parse_keyfile(my_priv_key, CONFIG_TLS_KEY_FILE, "")))
		{
			trx_tls_error_msg(res, "", &err_msg);
			treegix_log(LOG_LEVEL_CRIT, "cannot parse the private key in file \"%s\": %s",
					CONFIG_TLS_KEY_FILE, err_msg);
			trx_free(err_msg);
			trx_tls_free();
			exit(EXIT_FAILURE);
		}

		treegix_log(LOG_LEVEL_DEBUG, "%s() loaded " TRX_FS_SIZE_T "-bit %s private key from file \"%s\"",
				__func__, (trx_fs_size_t)pk_get_size(my_priv_key), pk_get_name(my_priv_key),
				CONFIG_TLS_KEY_FILE);
	}

	/* 'TLSPSKFile' parameter (in treegix_proxy.conf, treegix_agentd.conf). */
	/* Load pre-shared key. */
	if (NULL != CONFIG_TLS_PSK_FILE)
	{
		trx_read_psk_file();
		treegix_log(LOG_LEVEL_DEBUG, "%s() loaded PSK from file \"%s\"", __func__, CONFIG_TLS_PSK_FILE);
	}

	/* 'TLSPSKIdentity' parameter (in treegix_proxy.conf, treegix_agentd.conf). */
	/* Configure identity to be used with the pre-shared key. */
	if (NULL != CONFIG_TLS_PSK_IDENTITY)
	{
		my_psk_identity = CONFIG_TLS_PSK_IDENTITY;
		my_psk_identity_len = strlen(my_psk_identity);

		trx_check_psk_identity_len(my_psk_identity_len);

		treegix_log(LOG_LEVEL_DEBUG, "%s() loaded PSK identity \"%s\"", __func__, CONFIG_TLS_PSK_IDENTITY);
	}

	/* Certificate always comes from configuration file. Set up ciphersuites. */
	if (NULL != my_cert)
	{
		trx_ciphersuites(TRX_TLS_CIPHERSUITE_CERT, &ciphersuites_cert);
		trx_log_ciphersuites(__func__, "certificate", ciphersuites_cert);
	}

	/* PSK can come from configuration file (in proxy, agentd) and later from database (in server, proxy). */
	/* Configure ciphersuites just in case they will be used. */
	if (NULL != my_psk || 0 != (program_type & (TRX_PROGRAM_TYPE_SERVER | TRX_PROGRAM_TYPE_PROXY)))
	{
		trx_ciphersuites(TRX_TLS_CIPHERSUITE_PSK, &ciphersuites_psk);
		trx_log_ciphersuites(__func__, "PSK", ciphersuites_psk);
	}

	/* Sometimes we need to be ready for both certificate and PSK whichever comes in. Set up a combined list of */
	/* ciphersuites. */
	if (NULL != my_cert && (NULL != my_psk ||
			0 != (program_type & (TRX_PROGRAM_TYPE_SERVER | TRX_PROGRAM_TYPE_PROXY))))
	{
		trx_ciphersuites(TRX_TLS_CIPHERSUITE_ALL, &ciphersuites_all);
		trx_log_ciphersuites(__func__, "certificate and PSK", ciphersuites_all);
	}

	entropy = trx_malloc(entropy, sizeof(entropy_context));
	entropy_init(entropy);

	trx_make_personalization_string(pers);

	ctr_drbg = trx_malloc(ctr_drbg, sizeof(ctr_drbg_context));

	if (0 != (res = ctr_drbg_init(ctr_drbg, entropy_func, entropy, pers, 48)))
		/* PolarSSL sha512_finish() in SHA-384 mode returns an array "unsigned char output[64]" where result */
		/* resides in the first 48 bytes and the last 16 bytes are not used */
	{
		trx_guaranteed_memset(pers, 0, sizeof(pers));
		trx_tls_error_msg(res, "", &err_msg);
		treegix_log(LOG_LEVEL_CRIT, "cannot initialize random number generator: %s", err_msg);
		trx_free(err_msg);
		trx_tls_free();
		exit(EXIT_FAILURE);
	}

	trx_guaranteed_memset(pers, 0, sizeof(pers));

#ifndef _WINDOWS
	sigprocmask(SIG_SETMASK, &orig_mask, NULL);
#endif
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}
#elif defined(HAVE_GNUTLS)
void	trx_tls_init_child(void)
{
	int		res;
#ifndef _WINDOWS
	sigset_t	mask, orig_mask;
#endif
	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

#ifndef _WINDOWS
	/* Invalid TLS parameters will cause exit. Once one process exits the parent process will send SIGHUP to */
	/* child processes which may be on their way to exit on their own - do not interrupt them, block signal */
	/* SIGHUP and unblock it when TLS parameters are good and libraries are initialized. */
	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGHUP);
	sigaddset(&mask, SIGUSR2);
	sigaddset(&mask, SIGQUIT);
	sigprocmask(SIG_BLOCK, &mask, &orig_mask);

	trx_tls_library_init();		/* on Unix initialize crypto libraries in child processes */
#endif
	/* need to allocate certificate credentials store? */

	if (NULL != CONFIG_TLS_CERT_FILE)
	{
		if (GNUTLS_E_SUCCESS != (res = gnutls_certificate_allocate_credentials(&my_cert_creds)))
		{
			treegix_log(LOG_LEVEL_CRIT, "gnutls_certificate_allocate_credentials() failed: %d: %s", res,
					gnutls_strerror(res));
			trx_tls_free();
			exit(EXIT_FAILURE);
		}
	}

	/* 'TLSCAFile' parameter (in treegix_server.conf, treegix_proxy.conf, treegix_agentd.conf) */
	if (NULL != CONFIG_TLS_CA_FILE)
	{
		if (0 < (res = gnutls_certificate_set_x509_trust_file(my_cert_creds, CONFIG_TLS_CA_FILE,
				GNUTLS_X509_FMT_PEM)))
		{
			treegix_log(LOG_LEVEL_DEBUG, "%s() loaded %d CA certificate(s) from file \"%s\"",
					__func__, res, CONFIG_TLS_CA_FILE);
		}
		else if (0 == res)
		{
			treegix_log(LOG_LEVEL_WARNING, "no CA certificate(s) in file \"%s\"", CONFIG_TLS_CA_FILE);
		}
		else
		{
			treegix_log(LOG_LEVEL_CRIT, "cannot parse CA certificate(s) in file \"%s\": %d: %s",
				CONFIG_TLS_CA_FILE, res, gnutls_strerror(res));
			trx_tls_free();
			exit(EXIT_FAILURE);
		}
	}

	/* 'TLSCRLFile' parameter (in treegix_server.conf, treegix_proxy.conf, treegix_agentd.conf). */
	/* Load CRL (certificate revocation list) file. */
	if (NULL != CONFIG_TLS_CRL_FILE)
	{
		if (0 < (res = gnutls_certificate_set_x509_crl_file(my_cert_creds, CONFIG_TLS_CRL_FILE,
				GNUTLS_X509_FMT_PEM)))
		{
			treegix_log(LOG_LEVEL_DEBUG, "%s() loaded %d CRL(s) from file \"%s\"", __func__, res,
					CONFIG_TLS_CRL_FILE);
		}
		else if (0 == res)
		{
			treegix_log(LOG_LEVEL_WARNING, "no CRL(s) in file \"%s\"", CONFIG_TLS_CRL_FILE);
		}
		else
		{
			treegix_log(LOG_LEVEL_CRIT, "cannot parse CRL file \"%s\": %d: %s", CONFIG_TLS_CRL_FILE, res,
					gnutls_strerror(res));
			trx_tls_free();
			exit(EXIT_FAILURE);
		}
	}

	/* 'TLSCertFile' and 'TLSKeyFile' parameters (in treegix_server.conf, treegix_proxy.conf, treegix_agentd.conf). */
	/* Load certificate and private key. */
	if (NULL != CONFIG_TLS_CERT_FILE)
	{
		if (GNUTLS_E_SUCCESS != (res = gnutls_certificate_set_x509_key_file(my_cert_creds, CONFIG_TLS_CERT_FILE,
				CONFIG_TLS_KEY_FILE, GNUTLS_X509_FMT_PEM)))
		{
			treegix_log(LOG_LEVEL_CRIT, "cannot load certificate or private key from file \"%s\" or \"%s\":"
					" %d: %s", CONFIG_TLS_CERT_FILE, CONFIG_TLS_KEY_FILE, res,
					gnutls_strerror(res));
			trx_tls_free();
			exit(EXIT_FAILURE);
		}
		else
		{
			treegix_log(LOG_LEVEL_DEBUG, "%s() loaded certificate from file \"%s\"", __func__,
					CONFIG_TLS_CERT_FILE);
			treegix_log(LOG_LEVEL_DEBUG, "%s() loaded private key from file \"%s\"", __func__,
					CONFIG_TLS_KEY_FILE);
		}
	}

	/* 'TLSPSKIdentity' and 'TLSPSKFile' parameters (in treegix_proxy.conf, treegix_agentd.conf). */
	/* Load pre-shared key and identity to be used with the pre-shared key. */
	if (NULL != CONFIG_TLS_PSK_FILE)
	{
		gnutls_datum_t	key;

		my_psk_identity = CONFIG_TLS_PSK_IDENTITY;
		my_psk_identity_len = strlen(my_psk_identity);

		trx_check_psk_identity_len(my_psk_identity_len);

		trx_read_psk_file();

		key.data = (unsigned char *)my_psk;
		key.size = (unsigned int)my_psk_len;

		/* allocate here only PSK credential stores which do not change (e.g. for proxy communication with */
		/* server) */

		if (0 != (program_type & (TRX_PROGRAM_TYPE_PROXY_ACTIVE | TRX_PROGRAM_TYPE_AGENTD |
				TRX_PROGRAM_TYPE_SENDER | TRX_PROGRAM_TYPE_GET)))
		{
			if (GNUTLS_E_SUCCESS != (res = gnutls_psk_allocate_client_credentials(&my_psk_client_creds)))
			{
				treegix_log(LOG_LEVEL_CRIT, "gnutls_psk_allocate_client_credentials() failed: %d: %s",
						res, gnutls_strerror(res));
				trx_tls_free();
				exit(EXIT_FAILURE);
			}

			/* Simplified. 'my_psk_identity' should have been prepared as required by RFC 4518. */
			if (GNUTLS_E_SUCCESS != (res = gnutls_psk_set_client_credentials(my_psk_client_creds,
					my_psk_identity, &key, GNUTLS_PSK_KEY_RAW)))
			{
				treegix_log(LOG_LEVEL_CRIT, "gnutls_psk_set_client_credentials() failed: %d: %s", res,
						gnutls_strerror(res));
				trx_tls_free();
				exit(EXIT_FAILURE);
			}
		}

		if (0 != (program_type & (TRX_PROGRAM_TYPE_PROXY_PASSIVE | TRX_PROGRAM_TYPE_AGENTD)))
		{
			if (0 != (res = gnutls_psk_allocate_server_credentials(&my_psk_server_creds)))
			{
				treegix_log(LOG_LEVEL_CRIT, "gnutls_psk_allocate_server_credentials() failed: %d: %s",
						res, gnutls_strerror(res));
				trx_tls_free();
				exit(EXIT_FAILURE);
			}

			/* Apparently GnuTLS does not provide API for setting up static server credentials (with a */
			/* fixed PSK identity and key) for a passive proxy and agentd. The only possibility seems to */
			/* be to set up credentials dynamically for each incoming connection using a callback */
			/* function. */
			gnutls_psk_set_server_credentials_function(my_psk_server_creds, trx_psk_cb);
		}

		treegix_log(LOG_LEVEL_DEBUG, "%s() loaded PSK identity \"%s\"", __func__, CONFIG_TLS_PSK_IDENTITY);
		treegix_log(LOG_LEVEL_DEBUG, "%s() loaded PSK from file \"%s\"", __func__, CONFIG_TLS_PSK_FILE);
	}

	/* Certificate always comes from configuration file. Set up ciphersuites. */
	if (NULL != my_cert_creds)
	{
		/* this should work with GnuTLS 3.1.18 - 3.3.16 */
		if (GNUTLS_E_SUCCESS != (res = gnutls_priority_init(&ciphersuites_cert, "NONE:+VERS-TLS1.2:+ECDHE-RSA:"
				"+RSA:+AES-128-GCM:+AES-128-CBC:+AEAD:+SHA256:+SHA1:+CURVE-ALL:+COMP-NULL:+SIGN-ALL:"
				"+CTYPE-X.509", NULL)))
		{
			treegix_log(LOG_LEVEL_CRIT, "gnutls_priority_init() for 'ciphersuites_cert' failed: %d: %s",
					res, gnutls_strerror(res));
			trx_tls_free();
			exit(EXIT_FAILURE);
		}

		trx_log_ciphersuites(__func__, "certificate", ciphersuites_cert);
	}

	/* PSK can come from configuration file (in proxy, agentd) and later from database (in server, proxy). */
	/* Configure ciphersuites just in case they will be used. */
	if (NULL != my_psk_client_creds || NULL != my_psk_server_creds ||
			0 != (program_type & (TRX_PROGRAM_TYPE_SERVER | TRX_PROGRAM_TYPE_PROXY)))
	{
		/* this should work with GnuTLS 3.1.18 - 3.3.16 */
		if (GNUTLS_E_SUCCESS != (res = gnutls_priority_init(&ciphersuites_psk, "NONE:+VERS-TLS1.2:+ECDHE-PSK:"
				"+PSK:+AES-128-GCM:+AES-128-CBC:+AEAD:+SHA256:+SHA1:+CURVE-ALL:+COMP-NULL:+SIGN-ALL",
				NULL)))
		{
			treegix_log(LOG_LEVEL_CRIT, "gnutls_priority_init() for 'ciphersuites_psk' failed: %d: %s",
					res, gnutls_strerror(res));
			trx_tls_free();
			exit(EXIT_FAILURE);
		}

		trx_log_ciphersuites(__func__, "PSK", ciphersuites_psk);
	}

	/* Sometimes we need to be ready for both certificate and PSK whichever comes in. Set up a combined list of */
	/* ciphersuites. */
	if (NULL != my_cert_creds && (NULL != my_psk_client_creds || NULL != my_psk_server_creds ||
			0 != (program_type & (TRX_PROGRAM_TYPE_SERVER | TRX_PROGRAM_TYPE_PROXY))))
	{
		/* this should work with GnuTLS 3.1.18 - 3.3.16 */
		if (GNUTLS_E_SUCCESS != (res = gnutls_priority_init(&ciphersuites_all, "NONE:+VERS-TLS1.2:+ECDHE-RSA:"
				"+RSA:+ECDHE-PSK:+PSK:+AES-128-GCM:+AES-128-CBC:+AEAD:+SHA256:+SHA1:+CURVE-ALL:"
				"+COMP-NULL:+SIGN-ALL:+CTYPE-X.509", NULL)))
		{
			treegix_log(LOG_LEVEL_CRIT, "gnutls_priority_init() for 'ciphersuites_all' failed: %d: %s",
					res, gnutls_strerror(res));
			trx_tls_free();
			exit(EXIT_FAILURE);
		}

		trx_log_ciphersuites(__func__, "certificate and PSK", ciphersuites_all);
	}

#ifndef _WINDOWS
	sigprocmask(SIG_SETMASK, &orig_mask, NULL);
#endif
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}
#elif defined(HAVE_OPENSSL)
static const char	*trx_ctx_name(SSL_CTX *param)
{
	if (ctx_cert == param)
		return "certificate-based encryption";

#if defined(HAVE_OPENSSL_WITH_PSK)
	if (ctx_psk == param)
		return "PSK-based encryption";

	if (ctx_all == param)
		return "certificate and PSK-based encryption";
#endif
	THIS_SHOULD_NEVER_HAPPEN;
	return TRX_NULL2STR(NULL);
}

static int	trx_set_ecdhe_parameters(SSL_CTX *ctx)
{
	const char	*msg = "Perfect Forward Secrecy ECDHE ciphersuites will not be available for";
	EC_KEY		*ecdh;
	long		res;
	int		ret = SUCCEED;

	/* use curve secp256r1/prime256v1/NIST P-256 */

	if (NULL == (ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1)))
	{
		treegix_log(LOG_LEVEL_WARNING, "%s() EC_KEY_new_by_curve_name() failed. %s %s",
				__func__, msg, trx_ctx_name(ctx));
		return FAIL;
	}

	SSL_CTX_set_options(ctx, SSL_OP_SINGLE_ECDH_USE);

	if (1 != (res = SSL_CTX_set_tmp_ecdh(ctx, ecdh)))
	{
		treegix_log(LOG_LEVEL_WARNING, "%s() SSL_CTX_set_tmp_ecdh() returned %ld. %s %s",
				__func__, res, msg, trx_ctx_name(ctx));
		ret = FAIL;
	}

	EC_KEY_free(ecdh);

	return ret;
}

void	trx_tls_init_child(void)
{
#define TRX_CIPHERS_CERT_ECDHE		"EECDH+aRSA+AES128:"
#define TRX_CIPHERS_CERT		"RSA+aRSA+AES128"

#if defined(HAVE_OPENSSL_WITH_PSK)
#if OPENSSL_VERSION_NUMBER >= 0x1010100fL	/* OpenSSL 1.1.1 or newer */
	/* TLS_AES_256_GCM_SHA384 is excluded from client ciphersuite list for PSK based connections. */
	/* By default, in TLS 1.3 only *-SHA256 ciphersuites work with PSK. */
#	define TRX_CIPHERS_PSK_TLS13	"TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256"
#endif
#if OPENSSL_VERSION_NUMBER >= 0x1010000fL	/* OpenSSL 1.1.0 or newer */
#	define TRX_CIPHERS_PSK_ECDHE	"kECDHEPSK+AES128:"
#	define TRX_CIPHERS_PSK		"kPSK+AES128"
#else
#	define TRX_CIPHERS_PSK_ECDHE	""
#	define TRX_CIPHERS_PSK		"PSK-AES128-CBC-SHA"
#endif
#endif

	char	*error = NULL;
	size_t	error_alloc = 0, error_offset = 0;
#ifndef _WINDOWS
	sigset_t	mask, orig_mask;
#endif
	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

#ifndef _WINDOWS
	/* Invalid TLS parameters will cause exit. Once one process exits the parent process will send SIGHUP to */
	/* child processes which may be on their way to exit on their own - do not interrupt them, block signal */
	/* SIGHUP and unblock it when TLS parameters are good and libraries are initialized. */
	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGHUP);
	sigaddset(&mask, SIGUSR2);
	sigaddset(&mask, SIGQUIT);
	sigprocmask(SIG_BLOCK, &mask, &orig_mask);

	trx_tls_library_init();		/* on Unix initialize crypto libraries in child processes */
#endif
	if (1 != RAND_status())		/* protect against not properly seeded PRNG */
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot initialize PRNG");
		trx_tls_free();
		exit(EXIT_FAILURE);
	}

	/* set protocol version to TLS 1.2 */

	if (0 != (program_type & (TRX_PROGRAM_TYPE_SENDER | TRX_PROGRAM_TYPE_GET)))
		method = TLS_client_method();
	else	/* TRX_PROGRAM_TYPE_SERVER | TRX_PROGRAM_TYPE_PROXY | TRX_PROGRAM_TYPE_AGENTD */
		method = TLS_method();

	/* create context for certificate-only authentication if certificate is configured */
	if (NULL != CONFIG_TLS_CERT_FILE)
	{
		if (NULL == (ctx_cert = SSL_CTX_new(method)))
			goto out_method;

		if (1 != SSL_CTX_set_min_proto_version(ctx_cert, TLS1_2_VERSION))
			goto out_method;
	}
#if defined(HAVE_OPENSSL_WITH_PSK)
	/* Create context for PSK-only authentication. PSK can come from configuration file (in proxy, agentd) */
	/* and later from database (in server, proxy). */
	if (NULL != CONFIG_TLS_PSK_FILE || 0 != (program_type & (TRX_PROGRAM_TYPE_SERVER | TRX_PROGRAM_TYPE_PROXY)))
	{
		if (NULL == (ctx_psk = SSL_CTX_new(method)))
			goto out_method;

		if (1 != SSL_CTX_set_min_proto_version(ctx_psk, TLS1_2_VERSION))
			goto out_method;
	}

	/* Sometimes we need to be ready for both certificate and PSK whichever comes in. Set up a universal context */
	/* for certificate and PSK authentication to prepare for both. */
	if (NULL != ctx_cert && NULL != ctx_psk)
	{
		if (NULL == (ctx_all = SSL_CTX_new(method)))
			goto out_method;

		if (1 != SSL_CTX_set_min_proto_version(ctx_all, TLS1_2_VERSION))
			goto out_method;
	}
#endif
	/* 'TLSCAFile' parameter (in treegix_server.conf, treegix_proxy.conf, treegix_agentd.conf) */
	if (NULL != CONFIG_TLS_CA_FILE)
	{
#if defined(HAVE_OPENSSL_WITH_PSK)
		if (1 != SSL_CTX_load_verify_locations(ctx_cert, CONFIG_TLS_CA_FILE, NULL) ||
				(NULL != ctx_all && 1 != SSL_CTX_load_verify_locations(ctx_all, CONFIG_TLS_CA_FILE,
				NULL)))
#else
		if (1 != SSL_CTX_load_verify_locations(ctx_cert, CONFIG_TLS_CA_FILE, NULL))
#endif
		{
			trx_snprintf_alloc(&error, &error_alloc, &error_offset, "cannot load CA certificate(s) from"
					" file \"%s\":", CONFIG_TLS_CA_FILE);
			goto out;
		}

		treegix_log(LOG_LEVEL_DEBUG, "%s() loaded CA certificate(s) from file \"%s\"", __func__,
				CONFIG_TLS_CA_FILE);

		/* It is possible to limit the length of certificate chain being verified. For example: */
		/* SSL_CTX_set_verify_depth(ctx_cert, 2); */
		/* Currently use the default depth 100. */

		SSL_CTX_set_verify(ctx_cert, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);

#if defined(HAVE_OPENSSL_WITH_PSK)
		if (NULL != ctx_all)
			SSL_CTX_set_verify(ctx_all, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
#endif
	}

	/* 'TLSCRLFile' parameter (in treegix_server.conf, treegix_proxy.conf, treegix_agentd.conf). */
	/* Load CRL (certificate revocation list) file. */
	if (NULL != CONFIG_TLS_CRL_FILE)
	{
		X509_STORE	*store_cert;
		X509_LOOKUP	*lookup_cert;
		int		count_cert;

		store_cert = SSL_CTX_get_cert_store(ctx_cert);

		if (NULL == (lookup_cert = X509_STORE_add_lookup(store_cert, X509_LOOKUP_file())))
		{
			trx_snprintf_alloc(&error, &error_alloc, &error_offset, "X509_STORE_add_lookup() #%d failed"
					" when loading CRL(s) from file \"%s\":", 1, CONFIG_TLS_CRL_FILE);
			goto out;
		}

		if (0 >= (count_cert = X509_load_crl_file(lookup_cert, CONFIG_TLS_CRL_FILE, X509_FILETYPE_PEM)))
		{
			trx_snprintf_alloc(&error, &error_alloc, &error_offset, "cannot load CRL(s) from file \"%s\":",
					CONFIG_TLS_CRL_FILE);
			goto out;
		}

		if (1 != X509_STORE_set_flags(store_cert, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL))
		{
			trx_snprintf_alloc(&error, &error_alloc, &error_offset, "X509_STORE_set_flags() #%d failed when"
					" loading CRL(s) from file \"%s\":", 1, CONFIG_TLS_CRL_FILE);
			goto out;
		}
#if defined(HAVE_OPENSSL_WITH_PSK)
		if (NULL != ctx_all)
		{
			X509_STORE	*store_all;
			X509_LOOKUP	*lookup_all;
			int		count_all;

			store_all = SSL_CTX_get_cert_store(ctx_all);

			if (NULL == (lookup_all = X509_STORE_add_lookup(store_all, X509_LOOKUP_file())))
			{
				trx_snprintf_alloc(&error, &error_alloc, &error_offset, "X509_STORE_add_lookup() #%d"
						" failed when loading CRL(s) from file \"%s\":", 2,
						CONFIG_TLS_CRL_FILE);
				goto out;
			}

			if (0 >= (count_all = X509_load_crl_file(lookup_all, CONFIG_TLS_CRL_FILE, X509_FILETYPE_PEM)))
			{
				trx_snprintf_alloc(&error, &error_alloc, &error_offset, "cannot load CRL(s) from file"
						" \"%s\":", CONFIG_TLS_CRL_FILE);
				goto out;
			}

			if (count_cert != count_all)
			{
				trx_snprintf_alloc(&error, &error_alloc, &error_offset, "number of CRL(s) loaded from"
						" file \"%s\" does not match: %d and %d", CONFIG_TLS_CRL_FILE,
						count_cert, count_all);
				goto out1;
			}

			if (1 != X509_STORE_set_flags(store_all, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL))
			{
				trx_snprintf_alloc(&error, &error_alloc, &error_offset, "X509_STORE_set_flags() #%d"
						" failed when loading CRL(s) from file \"%s\":", 2,
						CONFIG_TLS_CRL_FILE);
				goto out;
			}
		}
#endif
		treegix_log(LOG_LEVEL_DEBUG, "%s() loaded %d CRL(s) from file \"%s\"", __func__, count_cert,
				CONFIG_TLS_CRL_FILE);
	}

	/* 'TLSCertFile' parameter (in treegix_server.conf, treegix_proxy.conf, treegix_agentd.conf). */
	/* Load certificate. */
	if (NULL != CONFIG_TLS_CERT_FILE)
	{
#if defined(HAVE_OPENSSL_WITH_PSK)
		if (1 != SSL_CTX_use_certificate_chain_file(ctx_cert, CONFIG_TLS_CERT_FILE) || (NULL != ctx_all &&
				1 != SSL_CTX_use_certificate_chain_file(ctx_all, CONFIG_TLS_CERT_FILE)))
#else
		if (1 != SSL_CTX_use_certificate_chain_file(ctx_cert, CONFIG_TLS_CERT_FILE))
#endif
		{
			trx_snprintf_alloc(&error, &error_alloc, &error_offset, "cannot load certificate(s) from file"
					" \"%s\":", CONFIG_TLS_CERT_FILE);
			goto out;
		}

		treegix_log(LOG_LEVEL_DEBUG, "%s() loaded certificate(s) from file \"%s\"", __func__,
				CONFIG_TLS_CERT_FILE);
	}

	/* 'TLSKeyFile' parameter (in treegix_server.conf, treegix_proxy.conf, treegix_agentd.conf). */
	/* Load private key. */
	if (NULL != CONFIG_TLS_KEY_FILE)
	{
#if defined(HAVE_OPENSSL_WITH_PSK)
		if (1 != SSL_CTX_use_PrivateKey_file(ctx_cert, CONFIG_TLS_KEY_FILE, SSL_FILETYPE_PEM) ||
				(NULL != ctx_all && 1 != SSL_CTX_use_PrivateKey_file(ctx_all, CONFIG_TLS_KEY_FILE,
				SSL_FILETYPE_PEM)))
#else
		if (1 != SSL_CTX_use_PrivateKey_file(ctx_cert, CONFIG_TLS_KEY_FILE, SSL_FILETYPE_PEM))
#endif
		{
			trx_snprintf_alloc(&error, &error_alloc, &error_offset, "cannot load private key from file"
					" \"%s\":", CONFIG_TLS_KEY_FILE);
			goto out;
		}

		treegix_log(LOG_LEVEL_DEBUG, "%s() loaded private key from file \"%s\"", __func__, CONFIG_TLS_KEY_FILE);

		if (1 != SSL_CTX_check_private_key(ctx_cert))
		{
			trx_snprintf_alloc(&error, &error_alloc, &error_offset, "certificate and private key do not"
					" match:");
			goto out;
		}
	}

	/* 'TLSPSKIdentity' and 'TLSPSKFile' parameters (in treegix_proxy.conf, treegix_agentd.conf). */
	/*  Load pre-shared key and identity to be used with the pre-shared key. */
	if (NULL != CONFIG_TLS_PSK_FILE)
	{
		my_psk_identity = CONFIG_TLS_PSK_IDENTITY;
		my_psk_identity_len = strlen(my_psk_identity);

		trx_check_psk_identity_len(my_psk_identity_len);

		treegix_log(LOG_LEVEL_DEBUG, "%s() loaded PSK identity \"%s\"", __func__, CONFIG_TLS_PSK_IDENTITY);

		trx_read_psk_file();

		treegix_log(LOG_LEVEL_DEBUG, "%s() loaded PSK from file \"%s\"", __func__, CONFIG_TLS_PSK_FILE);
	}
#if defined(HAVE_OPENSSL_WITH_PSK)
	/* set up PSK global variables for client callback if PSK comes only from configuration file or command line */

	if (NULL != ctx_psk && 0 != (program_type & (TRX_PROGRAM_TYPE_AGENTD | TRX_PROGRAM_TYPE_SENDER |
			TRX_PROGRAM_TYPE_GET)))
	{
		psk_identity_for_cb = my_psk_identity;
		psk_identity_len_for_cb = my_psk_identity_len;
		psk_for_cb = my_psk;
		psk_len_for_cb = my_psk_len;
	}
#endif
	if (NULL != ctx_cert)
	{
		const char	*ciphers;

		SSL_CTX_set_info_callback(ctx_cert, trx_openssl_info_cb);

		/* we're using blocking sockets, deal with renegotiations automatically */
		SSL_CTX_set_mode(ctx_cert, SSL_MODE_AUTO_RETRY);

		/* use server ciphersuite preference, do not use RFC 4507 ticket extension */
		SSL_CTX_set_options(ctx_cert, SSL_OP_CIPHER_SERVER_PREFERENCE | SSL_OP_NO_TICKET);

		/* do not connect to unpatched servers */
		SSL_CTX_clear_options(ctx_cert, SSL_OP_LEGACY_SERVER_CONNECT);

		/* disable session caching */
		SSL_CTX_set_session_cache_mode(ctx_cert, SSL_SESS_CACHE_OFF);

		/* try to enable ECDH ciphersuites */
		if (SUCCEED == trx_set_ecdhe_parameters(ctx_cert))
			ciphers = TRX_CIPHERS_CERT_ECDHE TRX_CIPHERS_CERT;
		else
			ciphers = TRX_CIPHERS_CERT;

		/* set up ciphersuites */
		if (1 != SSL_CTX_set_cipher_list(ctx_cert, ciphers))
		{
			trx_snprintf_alloc(&error, &error_alloc, &error_offset, "cannot set list of certificate"
					" ciphersuites:");
			goto out;
		}

		trx_log_ciphersuites(__func__, "certificate", ctx_cert);
	}
#if defined(HAVE_OPENSSL_WITH_PSK)
	if (NULL != ctx_psk)
	{
		const char	*ciphers;

		SSL_CTX_set_info_callback(ctx_psk, trx_openssl_info_cb);

		if (0 != (program_type & (TRX_PROGRAM_TYPE_SERVER | TRX_PROGRAM_TYPE_PROXY | TRX_PROGRAM_TYPE_AGENTD |
				TRX_PROGRAM_TYPE_SENDER | TRX_PROGRAM_TYPE_GET)))
		{
			SSL_CTX_set_psk_client_callback(ctx_psk, trx_psk_client_cb);
		}

		if (0 != (program_type & (TRX_PROGRAM_TYPE_SERVER | TRX_PROGRAM_TYPE_PROXY | TRX_PROGRAM_TYPE_AGENTD)))
			SSL_CTX_set_psk_server_callback(ctx_psk, trx_psk_server_cb);

		SSL_CTX_set_mode(ctx_psk, SSL_MODE_AUTO_RETRY);
		SSL_CTX_set_options(ctx_psk, SSL_OP_CIPHER_SERVER_PREFERENCE | SSL_OP_NO_TICKET);
		SSL_CTX_clear_options(ctx_psk, SSL_OP_LEGACY_SERVER_CONNECT);
		SSL_CTX_set_session_cache_mode(ctx_psk, SSL_SESS_CACHE_OFF);

		if ('\0' != *TRX_CIPHERS_PSK_ECDHE && SUCCEED == trx_set_ecdhe_parameters(ctx_psk))
			ciphers = TRX_CIPHERS_PSK_ECDHE TRX_CIPHERS_PSK;
		else
			ciphers = TRX_CIPHERS_PSK;

#if OPENSSL_VERSION_NUMBER >= 0x1010100fL	/* OpenSSL 1.1.1 or newer */
		if (1 != SSL_CTX_set_ciphersuites(ctx_psk, TRX_CIPHERS_PSK_TLS13))
		{
			trx_snprintf_alloc(&error, &error_alloc, &error_offset, "cannot set list of PSK TLS 1.3"
					"  ciphersuites:");
			goto out;
		}
#endif
		if (1 != SSL_CTX_set_cipher_list(ctx_psk, ciphers))
		{
			trx_snprintf_alloc(&error, &error_alloc, &error_offset, "cannot set list of PSK ciphersuites:");
			goto out;
		}

		trx_log_ciphersuites(__func__, "PSK", ctx_psk);
	}

	if (NULL != ctx_all)
	{
		const char	*ciphers;

		SSL_CTX_set_info_callback(ctx_all, trx_openssl_info_cb);

		if (0 != (program_type & (TRX_PROGRAM_TYPE_SERVER | TRX_PROGRAM_TYPE_PROXY | TRX_PROGRAM_TYPE_AGENTD)))
			SSL_CTX_set_psk_server_callback(ctx_all, trx_psk_server_cb);

		SSL_CTX_set_mode(ctx_all, SSL_MODE_AUTO_RETRY);
		SSL_CTX_set_options(ctx_all, SSL_OP_CIPHER_SERVER_PREFERENCE | SSL_OP_NO_TICKET);
		SSL_CTX_clear_options(ctx_all, SSL_OP_LEGACY_SERVER_CONNECT);
		SSL_CTX_set_session_cache_mode(ctx_all, SSL_SESS_CACHE_OFF);

		if (SUCCEED == trx_set_ecdhe_parameters(ctx_all))
			ciphers = TRX_CIPHERS_CERT_ECDHE TRX_CIPHERS_CERT ":" TRX_CIPHERS_PSK_ECDHE TRX_CIPHERS_PSK;
		else
			ciphers = TRX_CIPHERS_CERT ":" TRX_CIPHERS_PSK;

		if (1 != SSL_CTX_set_cipher_list(ctx_all, ciphers))
		{
			trx_snprintf_alloc(&error, &error_alloc, &error_offset, "cannot set list of all ciphersuites:");
			goto out;
		}

		trx_log_ciphersuites(__func__, "certificate and PSK", ctx_all);
	}
#endif /* defined(HAVE_OPENSSL_WITH_PSK) */
#ifndef _WINDOWS
	sigprocmask(SIG_SETMASK, &orig_mask, NULL);
#endif
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return;

out_method:
	trx_snprintf_alloc(&error, &error_alloc, &error_offset, "cannot initialize TLS method:");
out:
	trx_tls_error_msg(&error, &error_alloc, &error_offset);
#if defined(HAVE_OPENSSL_WITH_PSK)
out1:
#endif
	treegix_log(LOG_LEVEL_CRIT, "%s", error);
	trx_free(error);
	trx_tls_free();
	exit(EXIT_FAILURE);

#undef TRX_CIPHERS_CERT_ECDHE
#undef TRX_CIPHERS_CERT
#undef TRX_CIPHERS_PSK_ECDHE
#undef TRX_CIPHERS_PSK
#undef TRX_CIPHERS_PSK_TLS13
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: trx_tls_free_on_signal                                           *
 *                                                                            *
 * Purpose: TLS cleanup for using in signal handlers                          *
 *                                                                            *
 ******************************************************************************/
void	trx_tls_free_on_signal(void)
{
	if (NULL != my_psk)
		trx_guaranteed_memset(my_psk, 0, my_psk_len);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tls_free                                                     *
 *                                                                            *
 * Purpose: release TLS library resources allocated in trx_tls_init_parent()  *
 *          and trx_tls_init_child()                                          *
 *                                                                            *
 ******************************************************************************/
void	trx_tls_free(void)
{
#if defined(HAVE_POLARSSL)
	if (NULL != ctr_drbg)
	{
		ctr_drbg_free(ctr_drbg);
		trx_free(ctr_drbg);
	}

	if (NULL != entropy)
	{
		entropy_free(entropy);
		trx_free(entropy);
	}

	if (NULL != my_psk)
	{
		trx_guaranteed_memset(my_psk, 0, my_psk_len);
		my_psk_len = 0;
		trx_free(my_psk);
	}

	if (NULL != my_priv_key)
	{
		pk_free(my_priv_key);
		trx_free(my_priv_key);
	}

	if (NULL != my_cert)
	{
		x509_crt_free(my_cert);
		trx_free(my_cert);
	}

	if (NULL != crl)
	{
		x509_crl_free(crl);
		trx_free(crl);
	}

	if (NULL != ca_cert)
	{
		x509_crt_free(ca_cert);
		trx_free(ca_cert);
	}

	trx_free(ciphersuites_psk);
	trx_free(ciphersuites_cert);
	trx_free(ciphersuites_all);
#elif defined(HAVE_GNUTLS)
	if (NULL != my_cert_creds)
	{
		gnutls_certificate_free_credentials(my_cert_creds);
		my_cert_creds = NULL;
	}

	if (NULL != my_psk_client_creds)
	{
		gnutls_psk_free_client_credentials(my_psk_client_creds);
		my_psk_client_creds = NULL;
	}

	if (NULL != my_psk_server_creds)
	{
		gnutls_psk_free_server_credentials(my_psk_server_creds);
		my_psk_server_creds = NULL;
	}


	if (NULL != my_psk)
	{
		trx_guaranteed_memset(my_psk, 0, my_psk_len);
		my_psk_len = 0;
		trx_free(my_psk);
	}

#if !defined(_WINDOWS)
	trx_tls_library_deinit();
#endif
#elif defined(HAVE_OPENSSL)
	if (NULL != ctx_cert)
		SSL_CTX_free(ctx_cert);

#if defined(HAVE_OPENSSL_WITH_PSK)
	if (NULL != ctx_psk)
		SSL_CTX_free(ctx_psk);

	if (NULL != ctx_all)
		SSL_CTX_free(ctx_all);
#endif
	if (NULL != my_psk)
	{
		trx_guaranteed_memset(my_psk, 0, my_psk_len);
		my_psk_len = 0;
		trx_free(my_psk);
	}

#if !defined(_WINDOWS)
	trx_tls_library_deinit();
#endif
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tls_connect                                                  *
 *                                                                            *
 * Purpose: establish a TLS connection over an established TCP connection     *
 *                                                                            *
 * Parameters:                                                                *
 *     s           - [IN] socket with opened connection                       *
 *     error       - [OUT] dynamically allocated memory with error message    *
 *     tls_connect - [IN] how to connect. Allowed values:                     *
 *                        TRX_TCP_SEC_TLS_CERT, TRX_TCP_SEC_TLS_PSK.          *
 *     tls_arg1    - [IN] required issuer of peer certificate (may be NULL    *
 *                        or empty string if not important) or PSK identity   *
 *                        to connect with depending on value of               *
 *                        'tls_connect'.                                      *
 *     tls_arg2    - [IN] required subject of peer certificate (may be NULL   *
 *                        or empty string if not important) or PSK            *
 *                        (in hex-string) to connect with depending on value  *
 *                        of 'tls_connect'.                                   *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - successful TLS handshake with a valid certificate or PSK     *
 *     FAIL - an error occurred                                               *
 *                                                                            *
 ******************************************************************************/
#if defined(HAVE_POLARSSL)
int	trx_tls_connect(trx_socket_t *s, unsigned int tls_connect, const char *tls_arg1, const char *tls_arg2,
		char **error)
{
	int	ret = FAIL, res;
#if defined(_WINDOWS)
	double		sec;
#endif
	if (TRX_TCP_SEC_TLS_CERT == tls_connect)
	{
		treegix_log(LOG_LEVEL_DEBUG, "In %s(): issuer:\"%s\" subject:\"%s\"", __func__,
				TRX_NULL2EMPTY_STR(tls_arg1), TRX_NULL2EMPTY_STR(tls_arg2));

		if (NULL == ciphersuites_cert)
		{
			*error = trx_strdup(*error, "cannot connect with TLS and certificate: no valid certificate"
					" loaded");
			goto out1;
		}
	}
	else if (TRX_TCP_SEC_TLS_PSK == tls_connect)
	{
		treegix_log(LOG_LEVEL_DEBUG, "In %s(): psk_identity:\"%s\"", __func__, TRX_NULL2EMPTY_STR(tls_arg1));

		if (NULL == ciphersuites_psk)
		{
			*error = trx_strdup(*error, "cannot connect with TLS and PSK: no valid PSK loaded");
			goto out1;
		}
	}
	else
	{
		*error = trx_strdup(*error, "invalid connection parameters");
		THIS_SHOULD_NEVER_HAPPEN;
		goto out1;
	}

	/* set up TLS context */

	s->tls_ctx = trx_malloc(s->tls_ctx, sizeof(trx_tls_context_t));
	s->tls_ctx->ctx = trx_malloc(NULL, sizeof(ssl_context));

	if (0 != (res = ssl_init(s->tls_ctx->ctx)))
	{
		trx_tls_error_msg(res, "ssl_init(): ", error);
		goto out;
	}

	ssl_set_endpoint(s->tls_ctx->ctx, SSL_IS_CLIENT);

	/* Set RNG callback where to get random numbers from */
	ssl_set_rng(s->tls_ctx->ctx, ctr_drbg_random, ctr_drbg);

	/* disable using of session tickets (by default it is enabled on client) */
	if (0 != (res = ssl_set_session_tickets(s->tls_ctx->ctx, SSL_SESSION_TICKETS_DISABLED)))
	{
		trx_tls_error_msg(res, "ssl_set_session_tickets(): ", error);
		goto out;
	}

	if (SUCCEED == TRX_CHECK_LOG_LEVEL(LOG_LEVEL_TRACE))
	{
		/* Set our own debug callback function. The 3rd parameter of ssl_set_dbg() we set to NULL. It will be */
		/* passed as the 1st parameter to our callback function and will be ignored there. */
		ssl_set_dbg(s->tls_ctx->ctx, polarssl_debug_cb, NULL);

		/* For Treegix LOG_LEVEL_TRACE, PolarSSL debug level 3 seems the best. Recompile with 4 (apparently */
		/* the highest PolarSSL debug level) to dump also network raw bytes. */
		debug_set_threshold(3);
	}

	/* Set callback functions for receiving and sending data via socket. */
	/* Functions provided by PolarSSL work well so far, no need to invent our own. */
	ssl_set_bio(s->tls_ctx->ctx, net_recv, &s->socket, net_send, &s->socket);

	/* set protocol version to TLS 1.2 */
	ssl_set_min_version(s->tls_ctx->ctx, TRX_TLS_MIN_MAJOR_VER, TRX_TLS_MIN_MINOR_VER);
	ssl_set_max_version(s->tls_ctx->ctx, TRX_TLS_MAX_MAJOR_VER, TRX_TLS_MAX_MINOR_VER);

	if (TRX_TCP_SEC_TLS_CERT == tls_connect)	/* use certificates */
	{
		ssl_set_authmode(s->tls_ctx->ctx, SSL_VERIFY_REQUIRED);
		ssl_set_ciphersuites(s->tls_ctx->ctx, ciphersuites_cert);

		/* set CA certificate and certificate revocation lists */
		ssl_set_ca_chain(s->tls_ctx->ctx, ca_cert, crl, NULL);

		if (0 != (res = ssl_set_own_cert(s->tls_ctx->ctx, my_cert, my_priv_key)))
		{
			trx_tls_error_msg(res, "ssl_set_own_cert(): ", error);
			goto out;
		}
	}
	else	/* use a pre-shared key */
	{
		ssl_set_ciphersuites(s->tls_ctx->ctx, ciphersuites_psk);

		if (NULL == tls_arg2)	/* PSK is not set from DB */
		{
			/* set up the PSK from a configuration file (always in agentd and a case in active proxy */
			/* when it connects to server) */

			if (0 != (res = ssl_set_psk(s->tls_ctx->ctx, (const unsigned char *)my_psk, my_psk_len,
					(const unsigned char *)my_psk_identity, my_psk_identity_len)))
			{
				trx_tls_error_msg(res, "ssl_set_psk(): ", error);
				goto out;
			}
		}
		else
		{
			/* PSK comes from a database (case for a server/proxy when it connects to an agent for */
			/* passive checks, for a server when it connects to a passive proxy) */

			int	psk_len;
			char	psk_buf[HOST_TLS_PSK_LEN / 2];

			if (0 >= (psk_len = trx_psk_hex2bin((const unsigned char *)tls_arg2, (unsigned char *)psk_buf,
					sizeof(psk_buf))))
			{
				*error = trx_strdup(*error, "invalid PSK");
				goto out;
			}

			if (0 != (res = ssl_set_psk(s->tls_ctx->ctx, (const unsigned char *)psk_buf, (size_t)psk_len,
					(const unsigned char *)tls_arg1, strlen(tls_arg1))))
			{
				trx_tls_error_msg(res, "ssl_set_psk(): ", error);
				goto out;
			}
		}
	}

#if defined(_WINDOWS)
	trx_alarm_flag_clear();
	sec = trx_time();
#endif
	while (0 != (res = ssl_handshake(s->tls_ctx->ctx)))
	{
#if defined(_WINDOWS)
		if (s->timeout < trx_time() - sec)
			trx_alarm_flag_set();
#endif
		if (SUCCEED == trx_alarm_timed_out())
		{
			*error = trx_strdup(*error, "ssl_handshake() timed out");
			goto out;
		}

		if (POLARSSL_ERR_NET_WANT_READ != res && POLARSSL_ERR_NET_WANT_WRITE != res)
		{
			if (POLARSSL_ERR_X509_CERT_VERIFY_FAILED == res)
			{
				/* Standard PolarSSL error message might not be very informative in this case. For */
				/* example, if certificate validity starts in future, PolarSSL 1.3.9 would produce a */
				/* message "X509 - Certificate verification failed, e.g. CRL, CA or signature check */
				/* failed" which does not give a precise reason. Here we try to get more detailed */
				/* reason why peer certificate was rejected by using some knowledge about PolarSSL */
				/* internals. */
				trx_tls_cert_error_msg((unsigned int)s->tls_ctx->ctx->session_negotiate->verify_result,
						error);
				trx_tls_close(s);
				goto out1;
			}

			trx_tls_error_msg(res, "ssl_handshake(): ", error);
			goto out;
		}
	}

	if (TRX_TCP_SEC_TLS_CERT == tls_connect)
	{
		/* log peer certificate information for debugging */
		trx_log_peer_cert(__func__, s->tls_ctx);

		/* basic verification of peer certificate was done during handshake */

		/* if required verify peer certificate Issuer and Subject */
		if (SUCCEED != trx_verify_issuer_subject(s->tls_ctx, tls_arg1, tls_arg2, error))
		{
			trx_tls_close(s);
			goto out1;
		}
	}
	else	/* pre-shared key */
	{
		/* special print: s->tls_ctx->ctx->psk_identity is not '\0'-terminated */
		treegix_log(LOG_LEVEL_DEBUG, "%s() PSK identity: \"%.*s\"", __func__,
				(int)s->tls_ctx->ctx->psk_identity_len, s->tls_ctx->ctx->psk_identity);
	}

	s->connection_type = tls_connect;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():SUCCEED (established %s %s)", __func__,
			ssl_get_version(s->tls_ctx->ctx), ssl_get_ciphersuite(s->tls_ctx->ctx));

	return SUCCEED;

out:	/* an error occurred */
	ssl_free(s->tls_ctx->ctx);
	trx_free(s->tls_ctx->ctx);
	trx_free(s->tls_ctx);
out1:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s error:'%s'", __func__, trx_result_string(ret),
			TRX_NULL2EMPTY_STR(*error));
	return ret;
}
#elif defined(HAVE_GNUTLS)
int	trx_tls_connect(trx_socket_t *s, unsigned int tls_connect, const char *tls_arg1, const char *tls_arg2,
		char **error)
{
	int	ret = FAIL, res;
#if defined(_WINDOWS)
	double	sec;
#endif

	if (TRX_TCP_SEC_TLS_CERT == tls_connect)
	{
		treegix_log(LOG_LEVEL_DEBUG, "In %s(): issuer:\"%s\" subject:\"%s\"", __func__,
				TRX_NULL2EMPTY_STR(tls_arg1), TRX_NULL2EMPTY_STR(tls_arg2));
	}
	else if (TRX_TCP_SEC_TLS_PSK == tls_connect)
	{
		treegix_log(LOG_LEVEL_DEBUG, "In %s(): psk_identity:\"%s\"", __func__, TRX_NULL2EMPTY_STR(tls_arg1));
	}
	else
	{
		*error = trx_strdup(*error, "invalid connection parameters");
		THIS_SHOULD_NEVER_HAPPEN;
		goto out1;
	}

	/* set up TLS context */

	s->tls_ctx = trx_malloc(s->tls_ctx, sizeof(trx_tls_context_t));
	s->tls_ctx->ctx = NULL;
	s->tls_ctx->psk_client_creds = NULL;
	s->tls_ctx->psk_server_creds = NULL;

	if (GNUTLS_E_SUCCESS != (res = gnutls_init(&s->tls_ctx->ctx, GNUTLS_CLIENT | GNUTLS_NO_EXTENSIONS)))
			/* GNUTLS_NO_EXTENSIONS is used because we do not currently support extensions (e.g. session */
			/* tickets and OCSP) */
	{
		*error = trx_dsprintf(*error, "gnutls_init() failed: %d %s", res, gnutls_strerror(res));
		goto out;
	}

	if (TRX_TCP_SEC_TLS_CERT == tls_connect)
	{
		if (NULL == ciphersuites_cert)
		{
			*error = trx_strdup(*error, "cannot connect with TLS and certificate: no valid certificate"
					" loaded");
			goto out;
		}

		if (GNUTLS_E_SUCCESS != (res = gnutls_priority_set(s->tls_ctx->ctx, ciphersuites_cert)))
		{
			*error = trx_dsprintf(*error, "gnutls_priority_set() for 'ciphersuites_cert' failed: %d %s",
					res, gnutls_strerror(res));
			goto out;
		}

		if (GNUTLS_E_SUCCESS != (res = gnutls_credentials_set(s->tls_ctx->ctx, GNUTLS_CRD_CERTIFICATE,
				my_cert_creds)))
		{
			*error = trx_dsprintf(*error, "gnutls_credentials_set() for certificate failed: %d %s", res,
					gnutls_strerror(res));
			goto out;
		}
	}
	else	/* use a pre-shared key */
	{
		if (NULL == ciphersuites_psk)
		{
			*error = trx_strdup(*error, "cannot connect with TLS and PSK: no valid PSK loaded");
			goto out;
		}

		if (GNUTLS_E_SUCCESS != (res = gnutls_priority_set(s->tls_ctx->ctx, ciphersuites_psk)))
		{
			*error = trx_dsprintf(*error, "gnutls_priority_set() for 'ciphersuites_psk' failed: %d %s", res,
					gnutls_strerror(res));
			goto out;
		}

		if (NULL == tls_arg2)	/* PSK is not set from DB */
		{
			/* set up the PSK from a configuration file (always in agentd and a case in active proxy */
			/* when it connects to server) */

			if (GNUTLS_E_SUCCESS != (res = gnutls_credentials_set(s->tls_ctx->ctx, GNUTLS_CRD_PSK,
					my_psk_client_creds)))
			{
				*error = trx_dsprintf(*error, "gnutls_credentials_set() for psk failed: %d %s", res,
						gnutls_strerror(res));
				goto out;
			}
		}
		else
		{
			/* PSK comes from a database (case for a server/proxy when it connects to an agent for */
			/* passive checks, for a server when it connects to a passive proxy) */

			gnutls_datum_t	key;
			int		psk_len;
			unsigned char	psk_buf[HOST_TLS_PSK_LEN / 2];

			if (0 >= (psk_len = trx_psk_hex2bin((const unsigned char *)tls_arg2, psk_buf, sizeof(psk_buf))))
			{
				*error = trx_strdup(*error, "invalid PSK");
				goto out;
			}

			if (GNUTLS_E_SUCCESS != (res = gnutls_psk_allocate_client_credentials(
					&s->tls_ctx->psk_client_creds)))
			{
				*error = trx_dsprintf(*error, "gnutls_psk_allocate_client_credentials() failed: %d %s",
						res, gnutls_strerror(res));
				goto out;
			}

			key.data = psk_buf;
			key.size = (unsigned int)psk_len;

			/* Simplified. 'tls_arg1' (PSK identity) should have been prepared as required by RFC 4518. */
			if (GNUTLS_E_SUCCESS != (res = gnutls_psk_set_client_credentials(s->tls_ctx->psk_client_creds,
					tls_arg1, &key, GNUTLS_PSK_KEY_RAW)))
			{
				*error = trx_dsprintf(*error, "gnutls_psk_set_client_credentials() failed: %d %s", res,
						gnutls_strerror(res));
				goto out;
			}

			if (GNUTLS_E_SUCCESS != (res = gnutls_credentials_set(s->tls_ctx->ctx, GNUTLS_CRD_PSK,
					s->tls_ctx->psk_client_creds)))
			{
				*error = trx_dsprintf(*error, "gnutls_credentials_set() for psk failed: %d %s", res,
						gnutls_strerror(res));
				goto out;
			}
		}
	}

	if (SUCCEED == TRX_CHECK_LOG_LEVEL(LOG_LEVEL_TRACE))
	{
		/* set our own debug callback function */
		gnutls_global_set_log_function(trx_gnutls_debug_cb);

		/* for Treegix LOG_LEVEL_TRACE, GnuTLS debug level 4 seems the best */
		/* (the highest GnuTLS debug level is 9) */
		gnutls_global_set_log_level(4);
	}
	else
		gnutls_global_set_log_level(0);		/* restore default log level */

	/* set our own callback function to log issues into Treegix log */
	gnutls_global_set_audit_log_function(trx_gnutls_audit_cb);

	gnutls_transport_set_int(s->tls_ctx->ctx, TRX_SOCKET_TO_INT(s->socket));

	/* TLS handshake */

#if defined(_WINDOWS)
	trx_alarm_flag_clear();
	sec = trx_time();
#endif
	while (GNUTLS_E_SUCCESS != (res = gnutls_handshake(s->tls_ctx->ctx)))
	{
#if defined(_WINDOWS)
		if (s->timeout < trx_time() - sec)
			trx_alarm_flag_set();
#endif
		if (SUCCEED == trx_alarm_timed_out())
		{
			*error = trx_strdup(*error, "gnutls_handshake() timed out");
			goto out;
		}

		if (GNUTLS_E_INTERRUPTED == res || GNUTLS_E_AGAIN == res)
		{
			continue;
		}
		else if (GNUTLS_E_WARNING_ALERT_RECEIVED == res || GNUTLS_E_FATAL_ALERT_RECEIVED == res)
		{
			const char	*msg;
			int		alert;

			/* server sent an alert to us */
			alert = gnutls_alert_get(s->tls_ctx->ctx);

			if (NULL == (msg = gnutls_alert_get_name(alert)))
				msg = "unknown";

			if (GNUTLS_E_WARNING_ALERT_RECEIVED == res)
			{
				treegix_log(LOG_LEVEL_WARNING, "%s() gnutls_handshake() received a warning alert: %d %s",
						__func__, alert, msg);
				continue;
			}
			else	/* GNUTLS_E_FATAL_ALERT_RECEIVED */
			{
				*error = trx_dsprintf(*error, "%s(): gnutls_handshake() failed with fatal alert: %d %s",
						__func__, alert, msg);
				goto out;
			}
		}
		else
		{
			int	level;

			/* log "peer has closed connection" case with debug level */
			level = (GNUTLS_E_PREMATURE_TERMINATION == res ? LOG_LEVEL_DEBUG : LOG_LEVEL_WARNING);

			if (SUCCEED == TRX_CHECK_LOG_LEVEL(level))
			{
				treegix_log(level, "%s() gnutls_handshake() returned: %d %s",
						__func__, res, gnutls_strerror(res));
			}

			if (0 != gnutls_error_is_fatal(res))
			{
				*error = trx_dsprintf(*error, "%s(): gnutls_handshake() failed: %d %s",
						__func__, res, gnutls_strerror(res));
				goto out;
			}
		}
	}

	if (TRX_TCP_SEC_TLS_CERT == tls_connect)
	{
		/* log peer certificate information for debugging */
		trx_log_peer_cert(__func__, s->tls_ctx);

		/* perform basic verification of peer certificate */
		if (SUCCEED != trx_verify_peer_cert(s->tls_ctx->ctx, error))
		{
			trx_tls_close(s);
			goto out1;
		}

		/* if required verify peer certificate Issuer and Subject */
		if (SUCCEED != trx_verify_issuer_subject(s->tls_ctx, tls_arg1, tls_arg2, error))
		{
			trx_tls_close(s);
			goto out1;
		}
	}

	s->connection_type = tls_connect;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():SUCCEED (established %s %s-%s-%s-" TRX_FS_SIZE_T ")", __func__,
			gnutls_protocol_get_name(gnutls_protocol_get_version(s->tls_ctx->ctx)),
			gnutls_kx_get_name(gnutls_kx_get(s->tls_ctx->ctx)),
			gnutls_cipher_get_name(gnutls_cipher_get(s->tls_ctx->ctx)),
			gnutls_mac_get_name(gnutls_mac_get(s->tls_ctx->ctx)),
			(trx_fs_size_t)gnutls_mac_get_key_size(gnutls_mac_get(s->tls_ctx->ctx)));

	return SUCCEED;

out:	/* an error occurred */
	if (NULL != s->tls_ctx->ctx)
	{
		gnutls_credentials_clear(s->tls_ctx->ctx);
		gnutls_deinit(s->tls_ctx->ctx);
	}

	if (NULL != s->tls_ctx->psk_client_creds)
		gnutls_psk_free_client_credentials(s->tls_ctx->psk_client_creds);

	trx_free(s->tls_ctx);
out1:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s error:'%s'", __func__, trx_result_string(ret),
			TRX_NULL2EMPTY_STR(*error));
	return ret;
}
#elif defined(HAVE_OPENSSL)
int	trx_tls_connect(trx_socket_t *s, unsigned int tls_connect, const char *tls_arg1, const char *tls_arg2,
		char **error)
{
	int	ret = FAIL, res;
	size_t	error_alloc = 0, error_offset = 0;
#if defined(_WINDOWS)
	double	sec;
#endif
#if defined(HAVE_OPENSSL_WITH_PSK)
	char	psk_buf[HOST_TLS_PSK_LEN / 2];
#endif

	s->tls_ctx = trx_malloc(s->tls_ctx, sizeof(trx_tls_context_t));
	s->tls_ctx->ctx = NULL;

	if (TRX_TCP_SEC_TLS_CERT == tls_connect)
	{
		treegix_log(LOG_LEVEL_DEBUG, "In %s(): issuer:\"%s\" subject:\"%s\"", __func__,
				TRX_NULL2EMPTY_STR(tls_arg1), TRX_NULL2EMPTY_STR(tls_arg2));

		if (NULL == ctx_cert)
		{
			*error = trx_strdup(*error, "cannot connect with TLS and certificate: no valid certificate"
					" loaded");
			goto out;
		}

		if (NULL == (s->tls_ctx->ctx = SSL_new(ctx_cert)))
		{
			trx_snprintf_alloc(error, &error_alloc, &error_offset, "cannot create connection context:");
			trx_tls_error_msg(error, &error_alloc, &error_offset);
			goto out;
		}
	}
	else if (TRX_TCP_SEC_TLS_PSK == tls_connect)
	{
		treegix_log(LOG_LEVEL_DEBUG, "In %s(): psk_identity:\"%s\"", __func__, TRX_NULL2EMPTY_STR(tls_arg1));

#if defined(HAVE_OPENSSL_WITH_PSK)
		if (NULL == ctx_psk)
		{
			*error = trx_strdup(*error, "cannot connect with TLS and PSK: no valid PSK loaded");
			goto out;
		}

		if (NULL == (s->tls_ctx->ctx = SSL_new(ctx_psk)))
		{
			trx_snprintf_alloc(error, &error_alloc, &error_offset, "cannot create connection context:");
			trx_tls_error_msg(error, &error_alloc, &error_offset);
			goto out;
		}

		if (NULL == tls_arg2)	/* PSK is not set from DB */
		{
			/* Set up PSK global variables from a configuration file (always in agentd and a case when */
			/* active proxy connects to server). Here we set it only in case of active proxy */
			/* because for other programs it has already been set in trx_tls_init_child(). */

			if (0 != (program_type & TRX_PROGRAM_TYPE_PROXY_ACTIVE))
			{
				psk_identity_for_cb = my_psk_identity;
				psk_identity_len_for_cb = my_psk_identity_len;
				psk_for_cb = my_psk;
				psk_len_for_cb = my_psk_len;
			}
		}
		else
		{
			/* PSK comes from a database (case for a server/proxy when it connects to an agent for */
			/* passive checks, for a server when it connects to a passive proxy) */

			int	psk_len;

			if (0 >= (psk_len = trx_psk_hex2bin((const unsigned char *)tls_arg2, (unsigned char *)psk_buf,
					sizeof(psk_buf))))
			{
				*error = trx_strdup(*error, "invalid PSK");
				goto out;
			}

			/* some data reside in stack but it will be available at the time when a PSK client callback */
			/* function copies the data into buffers provided by OpenSSL within the callback */
			psk_identity_for_cb = tls_arg1;			/* string is on stack */
			/* NULL check to silence analyzer warning */
			psk_identity_len_for_cb = (NULL == tls_arg1 ? 0 : strlen(tls_arg1));
			psk_for_cb = psk_buf;				/* buffer is on stack */
			psk_len_for_cb = (size_t)psk_len;
		}
#else
		*error = trx_strdup(*error, "cannot connect with TLS and PSK: support for PSK was not compiled in");
		goto out;
#endif
	}
	else
	{
		*error = trx_strdup(*error, "invalid connection parameters");
		THIS_SHOULD_NEVER_HAPPEN;
		goto out1;
	}

	/* set our connected TCP socket to TLS context */
	if (1 != SSL_set_fd(s->tls_ctx->ctx, s->socket))
	{
		*error = trx_strdup(*error, "cannot set socket for TLS context");
		goto out;
	}

	/* TLS handshake */

	info_buf[0] = '\0';	/* empty buffer for trx_openssl_info_cb() messages */
#if defined(_WINDOWS)
	trx_alarm_flag_clear();
	sec = trx_time();
#endif
	if (1 != (res = SSL_connect(s->tls_ctx->ctx)))
	{
		int	result_code;

#if defined(_WINDOWS)
		if (s->timeout < trx_time() - sec)
			trx_alarm_flag_set();
#endif
		if (SUCCEED == trx_alarm_timed_out())
		{
			*error = trx_strdup(*error, "SSL_connect() timed out");
			goto out;
		}

		if (TRX_TCP_SEC_TLS_CERT == tls_connect)
		{
			long	verify_result;

			/* In case of certificate error SSL_get_verify_result() provides more helpful diagnostics */
			/* than other methods. Include it as first but continue with other diagnostics. */
			if (X509_V_OK != (verify_result = SSL_get_verify_result(s->tls_ctx->ctx)))
			{
				trx_snprintf_alloc(error, &error_alloc, &error_offset, "%s: ",
						X509_verify_cert_error_string(verify_result));
			}
		}

		result_code = SSL_get_error(s->tls_ctx->ctx, res);

		switch (result_code)
		{
			case SSL_ERROR_NONE:		/* handshake successful */
				break;
			case SSL_ERROR_ZERO_RETURN:
				trx_snprintf_alloc(error, &error_alloc, &error_offset,
						"TLS connection has been closed during handshake");
				goto out;
			case SSL_ERROR_SYSCALL:
				if (0 == ERR_peek_error())
				{
					if (0 == res)
					{
						trx_snprintf_alloc(error, &error_alloc, &error_offset,
								"connection closed by peer");
					}
					else if (-1 == res)
					{
						trx_snprintf_alloc(error, &error_alloc, &error_offset, "SSL_connect()"
								" I/O error: %s",
								strerror_from_system(trx_socket_last_error()));
					}
					else
					{
						/* "man SSL_get_error" describes only res == 0 and res == -1 for */
						/* SSL_ERROR_SYSCALL case */
						trx_snprintf_alloc(error, &error_alloc, &error_offset, "SSL_connect()"
								" returned undocumented code %d", res);
					}
				}
				else
				{
					trx_snprintf_alloc(error, &error_alloc, &error_offset, "SSL_connect() set"
							" result code to SSL_ERROR_SYSCALL:");
					trx_tls_error_msg(error, &error_alloc, &error_offset);
					trx_snprintf_alloc(error, &error_alloc, &error_offset, "%s", info_buf);
				}
				goto out;
			case SSL_ERROR_SSL:
				trx_snprintf_alloc(error, &error_alloc, &error_offset, "SSL_connect() set"
						" result code to SSL_ERROR_SSL:");
				trx_tls_error_msg(error, &error_alloc, &error_offset);
				trx_snprintf_alloc(error, &error_alloc, &error_offset, "%s", info_buf);
				goto out;
			default:
				trx_snprintf_alloc(error, &error_alloc, &error_offset, "SSL_connect() set result code"
						" to %d", result_code);
				trx_tls_error_msg(error, &error_alloc, &error_offset);
				trx_snprintf_alloc(error, &error_alloc, &error_offset, "%s", info_buf);
				goto out;
		}
	}

	if (TRX_TCP_SEC_TLS_CERT == tls_connect)
	{
		long	verify_result;

		/* log peer certificate information for debugging */
		trx_log_peer_cert(__func__, s->tls_ctx);

		/* perform basic verification of peer certificate */
		if (X509_V_OK != (verify_result = SSL_get_verify_result(s->tls_ctx->ctx)))
		{
			trx_snprintf_alloc(error, &error_alloc, &error_offset, "%s",
					X509_verify_cert_error_string(verify_result));
			trx_tls_close(s);
			goto out1;
		}

		/* if required verify peer certificate Issuer and Subject */
		if (SUCCEED != trx_verify_issuer_subject(s->tls_ctx, tls_arg1, tls_arg2, error))
		{
			trx_tls_close(s);
			goto out1;
		}
	}

	s->connection_type = tls_connect;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():SUCCEED (established %s %s)", __func__,
			SSL_get_version(s->tls_ctx->ctx), SSL_get_cipher(s->tls_ctx->ctx));

	return SUCCEED;

out:	/* an error occurred */
	if (NULL != s->tls_ctx->ctx)
		SSL_free(s->tls_ctx->ctx);

	trx_free(s->tls_ctx);
out1:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s error:'%s'", __func__, trx_result_string(ret),
			TRX_NULL2EMPTY_STR(*error));
	return ret;
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: trx_tls_accept                                                   *
 *                                                                            *
 * Purpose: establish a TLS connection over an accepted TCP connection        *
 *                                                                            *
 * Parameters:                                                                *
 *     s          - [IN] socket with opened connection                        *
 *     error      - [OUT] dynamically allocated memory with error message     *
 *     tls_accept - [IN] type of connection to accept. Can be be either       *
 *                       TRX_TCP_SEC_TLS_CERT or TRX_TCP_SEC_TLS_PSK, or      *
 *                       a bitwise 'OR' of both.                              *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - successful TLS handshake with a valid certificate or PSK     *
 *     FAIL - an error occurred                                               *
 *                                                                            *
 ******************************************************************************/
#if defined(HAVE_POLARSSL)
int	trx_tls_accept(trx_socket_t *s, unsigned int tls_accept, char **error)
{
	int			ret = FAIL, res;
	const ssl_ciphersuite_t	*info;
#if defined(_WINDOWS)
	double			sec;
#endif
	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	/* set up TLS context */

	s->tls_ctx = trx_malloc(s->tls_ctx, sizeof(trx_tls_context_t));
	s->tls_ctx->ctx = trx_malloc(NULL, sizeof(ssl_context));

	if (0 != (res = ssl_init(s->tls_ctx->ctx)))
	{
		trx_tls_error_msg(res, "ssl_init(): ", error);
		goto out;
	}

	ssl_set_endpoint(s->tls_ctx->ctx, SSL_IS_SERVER);

	/* Set RNG callback where to get random numbers from */
	ssl_set_rng(s->tls_ctx->ctx, ctr_drbg_random, ctr_drbg);

	/* explicitly disable using of session tickets (although by default it is disabled on server) */
	if (0 != (res = ssl_set_session_tickets(s->tls_ctx->ctx, SSL_SESSION_TICKETS_DISABLED)))
	{
		trx_tls_error_msg(res, "ssl_set_session_tickets(): ", error);
		goto out;
	}

	if (SUCCEED == TRX_CHECK_LOG_LEVEL(LOG_LEVEL_TRACE))
	{
		/* Set our own debug callback function. The 3rd parameter of ssl_set_dbg() we set to NULL. It will be */
		/* passed as the 1st parameter to our callback function and will be ignored there. */
		ssl_set_dbg(s->tls_ctx->ctx, polarssl_debug_cb, NULL);

		/* For Treegix LOG_LEVEL_TRACE, PolarSSL debug level 3 seems the best. Recompile with 4 (apparently */
		/* the highest PolarSSL debug level) to dump also network raw bytes. */
		debug_set_threshold(3);
	}

	/* Set callback functions for receiving and sending data via socket. */
	/* Functions provided by PolarSSL work well so far, no need to invent our own. */
	ssl_set_bio(s->tls_ctx->ctx, net_recv, &s->socket, net_send, &s->socket);

	/* set protocol version to TLS 1.2 */
	ssl_set_min_version(s->tls_ctx->ctx, TRX_TLS_MIN_MAJOR_VER, TRX_TLS_MIN_MINOR_VER);
	ssl_set_max_version(s->tls_ctx->ctx, TRX_TLS_MAX_MAJOR_VER, TRX_TLS_MAX_MINOR_VER);

	/* prepare to accept with certificate */

	if (0 != (tls_accept & TRX_TCP_SEC_TLS_CERT))
	{
		ssl_set_authmode(s->tls_ctx->ctx, SSL_VERIFY_REQUIRED);

		/* set CA certificate and certificate revocation lists */
		if (NULL != ca_cert)
			ssl_set_ca_chain(s->tls_ctx->ctx, ca_cert, crl, NULL);

		if (NULL != my_cert && 0 != (res = ssl_set_own_cert(s->tls_ctx->ctx, my_cert, my_priv_key)))
		{
			trx_tls_error_msg(res, "ssl_set_own_cert(): ", error);
			goto out;
		}
	}

	/* prepare to accept with pre-shared key */

	if (0 != (tls_accept & TRX_TCP_SEC_TLS_PSK))
	{
		/* for agentd the only possibility is a PSK from configuration file */
		if (0 != (program_type & TRX_PROGRAM_TYPE_AGENTD) &&
				0 != (res = ssl_set_psk(s->tls_ctx->ctx, (const unsigned char *)my_psk, my_psk_len,
				(const unsigned char *)my_psk_identity, my_psk_identity_len)))
		{
			trx_tls_error_msg(res, "ssl_set_psk(): ", error);
			goto out;
		}
		else if (0 != (program_type & (TRX_PROGRAM_TYPE_PROXY | TRX_PROGRAM_TYPE_SERVER)))
		{
			/* For server or proxy a PSK can come from configuration file or database. */
			/* Set up a callback function for finding the requested PSK. */
			ssl_set_psk_cb(s->tls_ctx->ctx, trx_psk_cb, NULL);
		}
	}

	/* set up ciphersuites */

	if ((TRX_TCP_SEC_TLS_CERT | TRX_TCP_SEC_TLS_PSK) == (tls_accept & (TRX_TCP_SEC_TLS_CERT | TRX_TCP_SEC_TLS_PSK)))
	{
		/* common case in trapper - be ready for all types of incoming connections */
		if (NULL != my_cert)
		{
			/* it can also be a case in agentd listener - when both certificate and PSK is allowed, e.g. */
			/* for switching of TLS connections from PSK to using a certificate */
			ssl_set_ciphersuites(s->tls_ctx->ctx, ciphersuites_all);
		}
		else
		{
			/* assume PSK, although it is not yet known will there be the right PSK available */
			ssl_set_ciphersuites(s->tls_ctx->ctx, ciphersuites_psk);
		}
	}
	else if (0 != (tls_accept & TRX_TCP_SEC_TLS_CERT) && NULL != my_cert)
		ssl_set_ciphersuites(s->tls_ctx->ctx, ciphersuites_cert);
	else if (0 != (tls_accept & TRX_TCP_SEC_TLS_PSK))
		ssl_set_ciphersuites(s->tls_ctx->ctx, ciphersuites_psk);

	/* TLS handshake */

#if defined(_WINDOWS)
	trx_alarm_flag_clear();
	sec = trx_time();
#endif
	while (0 != (res = ssl_handshake(s->tls_ctx->ctx)))
	{
#if defined(_WINDOWS)
		if (s->timeout < trx_time() - sec)
			trx_alarm_flag_set();
#endif
		if (SUCCEED == trx_alarm_timed_out())
		{
			*error = trx_strdup(*error, "ssl_handshake() timed out");
			goto out;
		}

		if (POLARSSL_ERR_NET_WANT_READ != res && POLARSSL_ERR_NET_WANT_WRITE != res)
		{
			if (POLARSSL_ERR_X509_CERT_VERIFY_FAILED == res)
			{
				/* Standard PolarSSL error message might not be very informative in this case. For */
				/* example, if certificate validity starts in future, PolarSSL 1.3.9 would produce a */
				/* message "X509 - Certificate verification failed, e.g. CRL, CA or signature check */
				/* failed" which does not give a precise reason. Here we try to get more detailed */
				/* reason why peer certificate was rejected by using some knowledge about PolarSSL */
				/* internals. */
				trx_tls_cert_error_msg((unsigned int)s->tls_ctx->ctx->session_negotiate->verify_result,
						error);
				trx_tls_close(s);
				goto out1;
			}

			trx_tls_error_msg(res, "ssl_handshake(): ", error);
			goto out;
		}
	}

	/* Is this TLS connection using certificate or PSK? */

	info = ssl_ciphersuite_from_id(s->tls_ctx->ctx->session->ciphersuite);

	if (POLARSSL_KEY_EXCHANGE_PSK == info->key_exchange ||
			POLARSSL_KEY_EXCHANGE_DHE_PSK == info->key_exchange ||
			POLARSSL_KEY_EXCHANGE_ECDHE_PSK == info->key_exchange ||
			POLARSSL_KEY_EXCHANGE_RSA_PSK == info->key_exchange)
	{
		s->connection_type = TRX_TCP_SEC_TLS_PSK;

		/* special print: s->tls_ctx->ctx->psk_identity is not '\0'-terminated */
		treegix_log(LOG_LEVEL_DEBUG, "%s() PSK identity: \"%.*s\"", __func__,
				(int)s->tls_ctx->ctx->psk_identity_len, s->tls_ctx->ctx->psk_identity);
	}
	else
	{
		s->connection_type = TRX_TCP_SEC_TLS_CERT;

		/* log peer certificate information for debugging */
		trx_log_peer_cert(__func__, s->tls_ctx);

		/* basic verification of peer certificate was done during handshake */

		/* Issuer and Subject will be verified later, after receiving sender type and host name */
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():SUCCEED (established %s %s)", __func__,
			ssl_get_version(s->tls_ctx->ctx), ssl_get_ciphersuite(s->tls_ctx->ctx));

	return SUCCEED;

out:	/* an error occurred */
	ssl_free(s->tls_ctx->ctx);
	trx_free(s->tls_ctx->ctx);
	trx_free(s->tls_ctx);
out1:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s error:'%s'", __func__, trx_result_string(ret),
			TRX_NULL2EMPTY_STR(*error));
	return ret;
}
#elif defined(HAVE_GNUTLS)
int	trx_tls_accept(trx_socket_t *s, unsigned int tls_accept, char **error)
{
	int				ret = FAIL, res;
	gnutls_credentials_type_t	creds;
#if defined(_WINDOWS)
	double				sec;
#endif
	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	/* set up TLS context */

	s->tls_ctx = trx_malloc(s->tls_ctx, sizeof(trx_tls_context_t));
	s->tls_ctx->ctx = NULL;
	s->tls_ctx->psk_client_creds = NULL;
	s->tls_ctx->psk_server_creds = NULL;

	if (GNUTLS_E_SUCCESS != (res = gnutls_init(&s->tls_ctx->ctx, GNUTLS_SERVER)))
	{
		*error = trx_dsprintf(*error, "gnutls_init() failed: %d %s", res, gnutls_strerror(res));
		goto out;
	}

	/* prepare to accept with certificate */

	if (0 != (tls_accept & TRX_TCP_SEC_TLS_CERT))
	{
		if (NULL != my_cert_creds && GNUTLS_E_SUCCESS != (res = gnutls_credentials_set(s->tls_ctx->ctx,
				GNUTLS_CRD_CERTIFICATE, my_cert_creds)))
		{
			*error = trx_dsprintf(*error, "gnutls_credentials_set() for certificate failed: %d %s", res,
					gnutls_strerror(res));
			goto out;
		}

		/* client certificate is mandatory unless pre-shared key is used */
		gnutls_certificate_server_set_request(s->tls_ctx->ctx, GNUTLS_CERT_REQUIRE);
	}

	/* prepare to accept with pre-shared key */

	if (0 != (tls_accept & TRX_TCP_SEC_TLS_PSK))
	{
		/* for agentd the only possibility is a PSK from configuration file */
		if (0 != (program_type & TRX_PROGRAM_TYPE_AGENTD) &&
				GNUTLS_E_SUCCESS != (res = gnutls_credentials_set(s->tls_ctx->ctx, GNUTLS_CRD_PSK,
				my_psk_server_creds)))
		{
			*error = trx_dsprintf(*error, "gnutls_credentials_set() for my_psk_server_creds failed: %d %s",
					res, gnutls_strerror(res));
			goto out;
		}
		else if (0 != (program_type & (TRX_PROGRAM_TYPE_PROXY | TRX_PROGRAM_TYPE_SERVER)))
		{
			/* For server or proxy a PSK can come from configuration file or database. */
			/* Set up a callback function for finding the requested PSK. */
			if (GNUTLS_E_SUCCESS != (res = gnutls_psk_allocate_server_credentials(
					&s->tls_ctx->psk_server_creds)))
			{
				*error = trx_dsprintf(*error, "gnutls_psk_allocate_server_credentials() for"
						" psk_server_creds failed: %d %s", res, gnutls_strerror(res));
				goto out;
			}

			gnutls_psk_set_server_credentials_function(s->tls_ctx->psk_server_creds, trx_psk_cb);

			if (GNUTLS_E_SUCCESS != (res = gnutls_credentials_set(s->tls_ctx->ctx, GNUTLS_CRD_PSK,
					s->tls_ctx->psk_server_creds)))
			{
				*error = trx_dsprintf(*error, "gnutls_credentials_set() for psk_server_creds failed"
						": %d %s", res, gnutls_strerror(res));
				goto out;
			}
		}
	}

	/* set up ciphersuites */

	if ((TRX_TCP_SEC_TLS_CERT | TRX_TCP_SEC_TLS_PSK) == (tls_accept & (TRX_TCP_SEC_TLS_CERT | TRX_TCP_SEC_TLS_PSK)))
	{
		/* common case in trapper - be ready for all types of incoming connections */
		if (NULL != my_cert_creds)
		{
			/* it can also be a case in agentd listener - when both certificate and PSK is allowed, e.g. */
			/* for switching of TLS connections from PSK to using a certificate */
			if (GNUTLS_E_SUCCESS != (res = gnutls_priority_set(s->tls_ctx->ctx, ciphersuites_all)))
			{
				*error = trx_dsprintf(*error, "gnutls_priority_set() for 'ciphersuites_all' failed: %d"
						" %s", res, gnutls_strerror(res));
				goto out;
			}
		}
		else
		{
			/* assume PSK, although it is not yet known will there be the right PSK available */
			if (GNUTLS_E_SUCCESS != (res = gnutls_priority_set(s->tls_ctx->ctx, ciphersuites_psk)))
			{
				*error = trx_dsprintf(*error, "gnutls_priority_set() for 'ciphersuites_psk' failed: %d"
						" %s", res, gnutls_strerror(res));
				goto out;
			}
		}
	}
	else if (0 != (tls_accept & TRX_TCP_SEC_TLS_CERT) && NULL != my_cert_creds)
	{
		if (GNUTLS_E_SUCCESS != (res = gnutls_priority_set(s->tls_ctx->ctx, ciphersuites_cert)))
		{
			*error = trx_dsprintf(*error, "gnutls_priority_set() for 'ciphersuites_cert' failed: %d %s",
					res, gnutls_strerror(res));
			goto out;
		}
	}
	else if (0 != (tls_accept & TRX_TCP_SEC_TLS_PSK))
	{
		if (GNUTLS_E_SUCCESS != (res = gnutls_priority_set(s->tls_ctx->ctx, ciphersuites_psk)))
		{
			*error = trx_dsprintf(*error, "gnutls_priority_set() for 'ciphersuites_psk' failed: %d %s", res,
					gnutls_strerror(res));
			goto out;
		}
	}

	if (SUCCEED == TRX_CHECK_LOG_LEVEL(LOG_LEVEL_TRACE))
	{
		/* set our own debug callback function */
		gnutls_global_set_log_function(trx_gnutls_debug_cb);

		/* for Treegix LOG_LEVEL_TRACE, GnuTLS debug level 4 seems the best */
		/* (the highest GnuTLS debug level is 9) */
		gnutls_global_set_log_level(4);
	}
	else
		gnutls_global_set_log_level(0);		/* restore default log level */

	/* set our own callback function to log issues into Treegix log */
	gnutls_global_set_audit_log_function(trx_gnutls_audit_cb);

	gnutls_transport_set_int(s->tls_ctx->ctx, TRX_SOCKET_TO_INT(s->socket));

	/* TLS handshake */

#if defined(_WINDOWS)
	trx_alarm_flag_clear();
	sec = trx_time();
#endif
	while (GNUTLS_E_SUCCESS != (res = gnutls_handshake(s->tls_ctx->ctx)))
	{
#if defined(_WINDOWS)
		if (s->timeout < trx_time() - sec)
			trx_alarm_flag_set();
#endif
		if (SUCCEED == trx_alarm_timed_out())
		{
			*error = trx_strdup(*error, "gnutls_handshake() timed out");
			goto out;
		}

		if (GNUTLS_E_INTERRUPTED == res || GNUTLS_E_AGAIN == res)
		{
			continue;
		}
		else if (GNUTLS_E_WARNING_ALERT_RECEIVED == res || GNUTLS_E_FATAL_ALERT_RECEIVED == res ||
				GNUTLS_E_GOT_APPLICATION_DATA == res)
		{
			const char	*msg;
			int		alert;

			/* client sent an alert to us */
			alert = gnutls_alert_get(s->tls_ctx->ctx);

			if (NULL == (msg = gnutls_alert_get_name(alert)))
				msg = "unknown";

			if (GNUTLS_E_WARNING_ALERT_RECEIVED == res)
			{
				treegix_log(LOG_LEVEL_WARNING, "%s() gnutls_handshake() received a warning alert: %d %s",
						__func__, alert, msg);
				continue;
			}
			else if (GNUTLS_E_GOT_APPLICATION_DATA == res)
					/* if rehandshake request deal with it as with error */
			{
				*error = trx_dsprintf(*error, "%s(): gnutls_handshake() returned"
						" GNUTLS_E_GOT_APPLICATION_DATA", __func__);
				goto out;
			}
			else	/* GNUTLS_E_FATAL_ALERT_RECEIVED */
			{
				*error = trx_dsprintf(*error, "%s(): gnutls_handshake() failed with fatal alert: %d %s",
						__func__, alert, msg);
				goto out;
			}
		}
		else
		{
			treegix_log(LOG_LEVEL_WARNING, "%s() gnutls_handshake() returned: %d %s",
					__func__, res, gnutls_strerror(res));

			if (0 != gnutls_error_is_fatal(res))
			{
				*error = trx_dsprintf(*error, "%s(): gnutls_handshake() failed: %d %s",
						__func__, res, gnutls_strerror(res));
				goto out;
			}
		}
	}

	/* Is this TLS connection using certificate or PSK? */

	if (GNUTLS_CRD_CERTIFICATE == (creds = gnutls_auth_get_type(s->tls_ctx->ctx)))
	{
		s->connection_type = TRX_TCP_SEC_TLS_CERT;

		/* log peer certificate information for debugging */
		trx_log_peer_cert(__func__, s->tls_ctx);

		/* perform basic verification of peer certificate */
		if (SUCCEED != trx_verify_peer_cert(s->tls_ctx->ctx, error))
		{
			trx_tls_close(s);
			goto out1;
		}

		/* Issuer and Subject will be verified later, after receiving sender type and host name */
	}
	else if (GNUTLS_CRD_PSK == creds)
	{
		s->connection_type = TRX_TCP_SEC_TLS_PSK;

		if (SUCCEED == TRX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
		{
			const char	*psk_identity;

			if (NULL != (psk_identity = gnutls_psk_server_get_username(s->tls_ctx->ctx)))
			{
				treegix_log(LOG_LEVEL_DEBUG, "%s() PSK identity: \"%s\"", __func__, psk_identity);
			}
		}
	}
	else
	{
		THIS_SHOULD_NEVER_HAPPEN;
		trx_tls_close(s);
		return FAIL;
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():SUCCEED (established %s %s-%s-%s-" TRX_FS_SIZE_T ")", __func__,
			gnutls_protocol_get_name(gnutls_protocol_get_version(s->tls_ctx->ctx)),
			gnutls_kx_get_name(gnutls_kx_get(s->tls_ctx->ctx)),
			gnutls_cipher_get_name(gnutls_cipher_get(s->tls_ctx->ctx)),
			gnutls_mac_get_name(gnutls_mac_get(s->tls_ctx->ctx)),
			(trx_fs_size_t)gnutls_mac_get_key_size(gnutls_mac_get(s->tls_ctx->ctx)));

	return SUCCEED;

out:	/* an error occurred */
	if (NULL != s->tls_ctx->ctx)
	{
		gnutls_credentials_clear(s->tls_ctx->ctx);
		gnutls_deinit(s->tls_ctx->ctx);
	}

	if (NULL != s->tls_ctx->psk_server_creds)
		gnutls_psk_free_server_credentials(s->tls_ctx->psk_server_creds);

	trx_free(s->tls_ctx);
out1:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s error:'%s'", __func__, trx_result_string(ret),
			TRX_NULL2EMPTY_STR(*error));
	return ret;
}
#elif defined(HAVE_OPENSSL)
int	trx_tls_accept(trx_socket_t *s, unsigned int tls_accept, char **error)
{
	const char	*cipher_name;
	int		ret = FAIL, res;
	size_t		error_alloc = 0, error_offset = 0;
	long		verify_result;
#if defined(_WINDOWS)
	double		sec;
#endif
#if OPENSSL_VERSION_NUMBER >= 0x1010100fL	/* OpenSSL 1.1.1 or newer, or LibreSSL */
	const unsigned char	session_id_context[] = {'Z', 'b', 'x'};
#endif
	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	s->tls_ctx = trx_malloc(s->tls_ctx, sizeof(trx_tls_context_t));
	s->tls_ctx->ctx = NULL;

#if defined(HAVE_OPENSSL_WITH_PSK)
	incoming_connection_has_psk = 0;	/* assume certificate-based connection by default */
#endif
	if ((TRX_TCP_SEC_TLS_CERT | TRX_TCP_SEC_TLS_PSK) == (tls_accept & (TRX_TCP_SEC_TLS_CERT | TRX_TCP_SEC_TLS_PSK)))
	{
#if defined(HAVE_OPENSSL_WITH_PSK)
		/* common case in trapper - be ready for all types of incoming connections but possible also in */
		/* agentd listener */

		if (NULL != ctx_all)
		{
			if (NULL == (s->tls_ctx->ctx = SSL_new(ctx_all)))
			{
				trx_snprintf_alloc(error, &error_alloc, &error_offset, "cannot create context to accept"
						" connection:");
				trx_tls_error_msg(error, &error_alloc, &error_offset);
				goto out;
			}
		}
#else
		if (0 != (program_type & (TRX_PROGRAM_TYPE_PROXY | TRX_PROGRAM_TYPE_SERVER)))
		{
			/* server or proxy running with OpenSSL or LibreSSL without PSK support */
			if (NULL != ctx_cert)
			{
				if (NULL == (s->tls_ctx->ctx = SSL_new(ctx_cert)))
				{
					trx_snprintf_alloc(error, &error_alloc, &error_offset, "cannot create context"
							" to accept connection:");
					trx_tls_error_msg(error, &error_alloc, &error_offset);
					goto out;
				}
			}
			else
			{
				*error = trx_strdup(*error, "not ready for certificate-based incoming connection:"
						" certificate not loaded. PSK support not compiled in.");
				goto out;
			}
		}
#endif
		else if (0 != (program_type & TRX_PROGRAM_TYPE_AGENTD))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			goto out;
		}
#if defined(HAVE_OPENSSL_WITH_PSK)
		else if (NULL != ctx_psk)
		{
			/* Server or proxy with no certificate configured. PSK is always assumed to be configured on */
			/* server or proxy because PSK can come from database. */

			if (NULL == (s->tls_ctx->ctx = SSL_new(ctx_psk)))
			{
				trx_snprintf_alloc(error, &error_alloc, &error_offset, "cannot create context to accept"
						" connection:");
				trx_tls_error_msg(error, &error_alloc, &error_offset);
				goto out;
			}
		}
		else
		{
			THIS_SHOULD_NEVER_HAPPEN;
			goto out;
		}
#endif
	}
	else if (0 != (tls_accept & TRX_TCP_SEC_TLS_CERT))
	{
		if (NULL != ctx_cert)
		{
			if (NULL == (s->tls_ctx->ctx = SSL_new(ctx_cert)))
			{
				trx_snprintf_alloc(error, &error_alloc, &error_offset, "cannot create context to accept"
						" connection:");
				trx_tls_error_msg(error, &error_alloc, &error_offset);
				goto out;
			}
		}
		else
		{
			*error = trx_strdup(*error, "not ready for certificate-based incoming connection: certificate"
					" not loaded");
			goto out;
		}
	}
	else	/* PSK */
	{
#if defined(HAVE_OPENSSL_WITH_PSK)
		if (NULL != ctx_psk)
		{
			if (NULL == (s->tls_ctx->ctx = SSL_new(ctx_psk)))
			{
				trx_snprintf_alloc(error, &error_alloc, &error_offset, "cannot create context to accept"
						" connection:");
				trx_tls_error_msg(error, &error_alloc, &error_offset);
				goto out;
			}
		}
		else
		{
			*error = trx_strdup(*error, "not ready for PSK-based incoming connection: PSK not loaded");
			goto out;
		}
#else
		*error = trx_strdup(*error, "support for PSK was not compiled in");
		goto out;
#endif
	}

#if OPENSSL_VERSION_NUMBER >= 0x1010100fL	/* OpenSSL 1.1.1 or newer, or LibreSSL */
	if (1 != SSL_set_session_id_context(s->tls_ctx->ctx, session_id_context, sizeof(session_id_context)))
	{
		*error = trx_strdup(*error, "cannot set session_id_context");
		goto out;
	}
#endif
	if (1 != SSL_set_fd(s->tls_ctx->ctx, s->socket))
	{
		*error = trx_strdup(*error, "cannot set socket for TLS context");
		goto out;
	}

	/* TLS handshake */

	info_buf[0] = '\0';	/* empty buffer for trx_openssl_info_cb() messages */
#if defined(_WINDOWS)
	trx_alarm_flag_clear();
	sec = trx_time();
#endif
	if (1 != (res = SSL_accept(s->tls_ctx->ctx)))
	{
		int	result_code;

#if defined(_WINDOWS)
		if (s->timeout < trx_time() - sec)
			trx_alarm_flag_set();
#endif
		if (SUCCEED == trx_alarm_timed_out())
		{
			*error = trx_strdup(*error, "SSL_accept() timed out");
			goto out;
		}

		/* In case of certificate error SSL_get_verify_result() provides more helpful diagnostics */
		/* than other methods. Include it as first but continue with other diagnostics. Should be */
		/* harmless in case of PSK. */

		if (X509_V_OK != (verify_result = SSL_get_verify_result(s->tls_ctx->ctx)))
		{
			trx_snprintf_alloc(error, &error_alloc, &error_offset, "%s: ",
					X509_verify_cert_error_string(verify_result));
		}

		result_code = SSL_get_error(s->tls_ctx->ctx, res);

		if (0 == res)
		{
			trx_snprintf_alloc(error, &error_alloc, &error_offset, "TLS connection has been closed during"
					" handshake:");
		}
		else
		{
			trx_snprintf_alloc(error, &error_alloc, &error_offset, "TLS handshake set result code to %d:",
					result_code);
		}

		trx_tls_error_msg(error, &error_alloc, &error_offset);
		trx_snprintf_alloc(error, &error_alloc, &error_offset, "%s", info_buf);
		goto out;
	}

	/* Is this TLS connection using certificate or PSK? */

	cipher_name = SSL_get_cipher(s->tls_ctx->ctx);

#if defined(HAVE_OPENSSL_WITH_PSK)
	if (1 == incoming_connection_has_psk)
	{
		s->connection_type = TRX_TCP_SEC_TLS_PSK;
	}
	else if (0 != strncmp("(NONE)", cipher_name, TRX_CONST_STRLEN("(NONE)")))
#endif
	{
		s->connection_type = TRX_TCP_SEC_TLS_CERT;

		/* log peer certificate information for debugging */
		trx_log_peer_cert(__func__, s->tls_ctx);

		/* perform basic verification of peer certificate */
		if (X509_V_OK != (verify_result = SSL_get_verify_result(s->tls_ctx->ctx)))
		{
			trx_snprintf_alloc(error, &error_alloc, &error_offset, "%s",
					X509_verify_cert_error_string(verify_result));
			trx_tls_close(s);
			goto out1;
		}

		/* Issuer and Subject will be verified later, after receiving sender type and host name */
	}
#if defined(HAVE_OPENSSL_WITH_PSK)
	else
	{
		THIS_SHOULD_NEVER_HAPPEN;
		trx_tls_close(s);
		return FAIL;
	}
#endif
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():SUCCEED (established %s %s)", __func__,
			SSL_get_version(s->tls_ctx->ctx), cipher_name);

	return SUCCEED;

out:	/* an error occurred */
	if (NULL != s->tls_ctx->ctx)
		SSL_free(s->tls_ctx->ctx);

	trx_free(s->tls_ctx);
out1:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s error:'%s'", __func__, trx_result_string(ret),
			TRX_NULL2EMPTY_STR(*error));
	return ret;
}
#endif

#if defined(HAVE_POLARSSL)
#	define TRX_TLS_WRITE(ctx, buf, len)	ssl_write(ctx, (const unsigned char *)(buf), len)
#	define TRX_TLS_READ(ctx, buf, len)	ssl_read(ctx, (unsigned char *)(buf), len)
#	define TRX_TLS_WRITE_FUNC_NAME		"ssl_write"
#	define TRX_TLS_READ_FUNC_NAME		"ssl_read"
#	define TRX_TLS_WANT_WRITE(res)		(POLARSSL_ERR_NET_WANT_WRITE == (res) ? SUCCEED : FAIL)
#	define TRX_TLS_WANT_READ(res)		(POLARSSL_ERR_NET_WANT_READ == (res) ? SUCCEED : FAIL)
#elif defined(HAVE_GNUTLS)
#	define TRX_TLS_WRITE(ctx, buf, len)	gnutls_record_send(ctx, buf, len)
#	define TRX_TLS_READ(ctx, buf, len)	gnutls_record_recv(ctx, buf, len)
#	define TRX_TLS_WRITE_FUNC_NAME		"gnutls_record_send"
#	define TRX_TLS_READ_FUNC_NAME		"gnutls_record_recv"
#	define TRX_TLS_WANT_WRITE(res)		(GNUTLS_E_INTERRUPTED == (res) || GNUTLS_E_AGAIN == (res) ? SUCCEED : FAIL)
#	define TRX_TLS_WANT_READ(res)		(GNUTLS_E_INTERRUPTED == (res) || GNUTLS_E_AGAIN == (res) ? SUCCEED : FAIL)
#elif defined(HAVE_OPENSSL)
#	define TRX_TLS_WRITE(ctx, buf, len)	SSL_write(ctx, buf, (int)(len))
#	define TRX_TLS_READ(ctx, buf, len)	SSL_read(ctx, buf, (int)(len))
#	define TRX_TLS_WRITE_FUNC_NAME		"SSL_write"
#	define TRX_TLS_READ_FUNC_NAME		"SSL_read"
#	define TRX_TLS_WANT_WRITE(res)		FAIL
#	define TRX_TLS_WANT_READ(res)		FAIL
/* SSL_ERROR_WANT_READ or SSL_ERROR_WANT_WRITE should not be returned here because we set */
/* SSL_MODE_AUTO_RETRY flag in trx_tls_init_child() */
#endif

ssize_t	trx_tls_write(trx_socket_t *s, const char *buf, size_t len, char **error)
{
#if defined(_WINDOWS)
	double	sec;
#endif
#if defined(HAVE_POLARSSL)
	int	res;
#elif defined(HAVE_GNUTLS)
	ssize_t	res;
#elif defined(HAVE_OPENSSL)
	int	res;
#endif

#if defined(_WINDOWS)
	trx_alarm_flag_clear();
	sec = trx_time();
#endif
#if defined(HAVE_OPENSSL)
	info_buf[0] = '\0';	/* empty buffer for trx_openssl_info_cb() messages */
#endif
	do
	{
		res = TRX_TLS_WRITE(s->tls_ctx->ctx, buf, len);
#if defined(_WINDOWS)
		if (s->timeout < trx_time() - sec)
			trx_alarm_flag_set();
#endif
		if (SUCCEED == trx_alarm_timed_out())
		{
			*error = trx_strdup(*error, TRX_TLS_WRITE_FUNC_NAME "() timed out");
			return TRX_PROTO_ERROR;
		}
	}
	while (SUCCEED == TRX_TLS_WANT_WRITE(res));

#if defined(HAVE_POLARSSL)
	if (0 > res)
	{
		char	err[128];	/* 128 bytes are enough for PolarSSL error messages */

		polarssl_strerror(res, err, sizeof(err));
		*error = trx_dsprintf(*error, "ssl_write() failed: %s", err);

		return TRX_PROTO_ERROR;
	}
#elif defined(HAVE_GNUTLS)
	if (0 > res)
	{
		*error = trx_dsprintf(*error, "gnutls_record_send() failed: " TRX_FS_SSIZE_T " %s",
				(trx_fs_ssize_t)res, gnutls_strerror(res));

		return TRX_PROTO_ERROR;
	}
#elif defined(HAVE_OPENSSL)
	if (0 >= res)
	{
		int	result_code;

		result_code = SSL_get_error(s->tls_ctx->ctx, res);

		if (0 == res && SSL_ERROR_ZERO_RETURN == result_code)
		{
			*error = trx_strdup(*error, "connection closed during write");
		}
		else
		{
			char	*err = NULL;
			size_t	error_alloc = 0, error_offset = 0;

			trx_snprintf_alloc(&err, &error_alloc, &error_offset, "TLS write set result code to"
					" %d:", result_code);
			trx_tls_error_msg(&err, &error_alloc, &error_offset);
			*error = trx_dsprintf(*error, "%s%s", err, info_buf);
			trx_free(err);
		}

		return TRX_PROTO_ERROR;
	}
#endif

	return (ssize_t)res;
}

ssize_t	trx_tls_read(trx_socket_t *s, char *buf, size_t len, char **error)
{
#if defined(_WINDOWS)
	double	sec;
#endif
#if defined(HAVE_POLARSSL)
	int	res;
#elif defined(HAVE_GNUTLS)
	ssize_t	res;
#elif defined(HAVE_OPENSSL)
	int	res;
#endif

#if defined(_WINDOWS)
	trx_alarm_flag_clear();
	sec = trx_time();
#endif
#if defined(HAVE_OPENSSL)
	info_buf[0] = '\0';	/* empty buffer for trx_openssl_info_cb() messages */
#endif
	do
	{
		res = TRX_TLS_READ(s->tls_ctx->ctx, buf, len);
#if defined(_WINDOWS)
		if (s->timeout < trx_time() - sec)
			trx_alarm_flag_set();
#endif
		if (SUCCEED == trx_alarm_timed_out())
		{
			*error = trx_strdup(*error, TRX_TLS_READ_FUNC_NAME "() timed out");
			return TRX_PROTO_ERROR;
		}
	}
	while (SUCCEED == TRX_TLS_WANT_READ(res));

#if defined(HAVE_POLARSSL)
	if (0 > res)
	{
		char	err[128];	/* 128 bytes are enough for PolarSSL error messages */

		polarssl_strerror(res, err, sizeof(err));
		*error = trx_dsprintf(*error, "ssl_read() failed: %s", err);

		return TRX_PROTO_ERROR;
	}
#elif defined(HAVE_GNUTLS)
	if (0 > res)
	{
		/* in case of rehandshake a GNUTLS_E_REHANDSHAKE will be returned, deal with it as with error */
		*error = trx_dsprintf(*error, "gnutls_record_recv() failed: " TRX_FS_SSIZE_T " %s",
				(trx_fs_ssize_t)res, gnutls_strerror(res));

		return TRX_PROTO_ERROR;
	}
#elif defined(HAVE_OPENSSL)
	if (0 >= res)
	{
		int	result_code;

		result_code = SSL_get_error(s->tls_ctx->ctx, res);

		if (0 == res && SSL_ERROR_ZERO_RETURN == result_code)
		{
			*error = trx_strdup(*error, "connection closed during read");
		}
		else
		{
			char	*err = NULL;
			size_t	error_alloc = 0, error_offset = 0;

			trx_snprintf_alloc(&err, &error_alloc, &error_offset, "TLS read set result code to"
					" %d:", result_code);
			trx_tls_error_msg(&err, &error_alloc, &error_offset);
			*error = trx_dsprintf(*error, "%s%s", err, info_buf);
			trx_free(err);
		}

		return TRX_PROTO_ERROR;
	}
#endif

	return (ssize_t)res;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tls_close                                                    *
 *                                                                            *
 * Purpose: close a TLS connection before closing a TCP socket                *
 *                                                                            *
 ******************************************************************************/
void	trx_tls_close(trx_socket_t *s)
{
	int	res;

	if (NULL == s->tls_ctx)
		return;
#if defined(HAVE_POLARSSL)
	if (NULL != s->tls_ctx->ctx)
	{
#if defined(_WINDOWS)
		double	sec;

		trx_alarm_flag_clear();
		sec = trx_time();
#endif
		while (0 > (res = ssl_close_notify(s->tls_ctx->ctx)))
		{
#if defined(_WINDOWS)
			if (s->timeout < trx_time() - sec)
				trx_alarm_flag_set();
#endif
			if (SUCCEED == trx_alarm_timed_out())
				break;

			if (POLARSSL_ERR_NET_WANT_READ != res && POLARSSL_ERR_NET_WANT_WRITE != res)
			{
				treegix_log(LOG_LEVEL_WARNING, "ssl_close_notify() with %s returned error code: %d",
						s->peer, res);
				break;
			}
		}

		ssl_free(s->tls_ctx->ctx);
		trx_free(s->tls_ctx->ctx);
	}
#elif defined(HAVE_GNUTLS)
	if (NULL != s->tls_ctx->ctx)
	{
#if defined(_WINDOWS)
		double	sec;

		trx_alarm_flag_clear();
		sec = trx_time();
#endif
		/* shutdown TLS connection */
		while (GNUTLS_E_SUCCESS != (res = gnutls_bye(s->tls_ctx->ctx, GNUTLS_SHUT_WR)))
		{
#if defined(_WINDOWS)
			if (s->timeout < trx_time() - sec)
				trx_alarm_flag_set();
#endif
			if (SUCCEED == trx_alarm_timed_out())
				break;

			if (GNUTLS_E_INTERRUPTED == res || GNUTLS_E_AGAIN == res)
				continue;

			treegix_log(LOG_LEVEL_WARNING, "gnutls_bye() with %s returned error code: %d %s",
					s->peer, res, gnutls_strerror(res));

			if (0 != gnutls_error_is_fatal(res))
				break;
		}

		gnutls_credentials_clear(s->tls_ctx->ctx);
		gnutls_deinit(s->tls_ctx->ctx);
	}

	if (NULL != s->tls_ctx->psk_client_creds)
		gnutls_psk_free_client_credentials(s->tls_ctx->psk_client_creds);

	if (NULL != s->tls_ctx->psk_server_creds)
		gnutls_psk_free_server_credentials(s->tls_ctx->psk_server_creds);
#elif defined(HAVE_OPENSSL)
	if (NULL != s->tls_ctx->ctx)
	{
		info_buf[0] = '\0';	/* empty buffer for trx_openssl_info_cb() messages */

		/* After TLS shutdown the TCP connection will be closed. So, there is no need to do a bidirectional */
		/* TLS shutdown - unidirectional shutdown is ok. */
		if (0 > (res = SSL_shutdown(s->tls_ctx->ctx)))
		{
			int	result_code;
			char	*error = NULL;
			size_t	error_alloc = 0, error_offset = 0;

			result_code = SSL_get_error(s->tls_ctx->ctx, res);
			trx_tls_error_msg(&error, &error_alloc, &error_offset);
			treegix_log(LOG_LEVEL_WARNING, "SSL_shutdown() with %s set result code to %d:%s%s",
					s->peer, result_code, TRX_NULL2EMPTY_STR(error), info_buf);
			trx_free(error);
		}

		SSL_free(s->tls_ctx->ctx);
	}
#endif
	trx_free(s->tls_ctx);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tls_get_attr_cert                                            *
 *                                                                            *
 * Purpose: get certificate attributes from the context of established        *
 *          connection                                                        *
 *                                                                            *
 ******************************************************************************/
int	trx_tls_get_attr_cert(const trx_socket_t *s, trx_tls_conn_attr_t *attr)
{
	char			*error = NULL;
#if defined(HAVE_POLARSSL)
	const x509_crt		*peer_cert;
#elif defined(HAVE_GNUTLS)
	gnutls_x509_crt_t	peer_cert;
	gnutls_x509_dn_t	dn;
	int			res;
#elif defined(HAVE_OPENSSL)
	X509			*peer_cert;
#endif

#if defined(HAVE_POLARSSL)
	if (NULL == (peer_cert = ssl_get_peer_cert(s->tls_ctx->ctx)))
	{
		treegix_log(LOG_LEVEL_WARNING, "no peer certificate, ssl_get_peer_cert() returned NULL");
		return FAIL;
	}

	if (SUCCEED != trx_x509_dn_gets(&peer_cert->issuer, attr->issuer, sizeof(attr->issuer), &error))
	{
		treegix_log(LOG_LEVEL_WARNING, "error while getting issuer name: \"%s\"", error);
		trx_free(error);
		return FAIL;
	}

	if (SUCCEED != trx_x509_dn_gets(&peer_cert->subject, attr->subject, sizeof(attr->subject), &error))
	{
		treegix_log(LOG_LEVEL_WARNING, "error while getting subject name: \"%s\"", error);
		trx_free(error);
		return FAIL;
	}
#elif defined(HAVE_GNUTLS)
	/* here is some inefficiency - we do not know will it be required to verify peer certificate issuer */
	/* and subject - but we prepare for it */
	if (NULL == (peer_cert = trx_get_peer_cert(s->tls_ctx->ctx, &error)))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot get peer certificate: %s", error);
		trx_free(error);
		return FAIL;
	}

	if (0 != (res = gnutls_x509_crt_get_issuer(peer_cert, &dn)))
	{
		treegix_log(LOG_LEVEL_WARNING, "gnutls_x509_crt_get_issuer() failed: %d %s", res,
				gnutls_strerror(res));
		gnutls_x509_crt_deinit(peer_cert);
		return FAIL;
	}

	if (SUCCEED != trx_x509_dn_gets(dn, attr->issuer, sizeof(attr->issuer), &error))
	{
		treegix_log(LOG_LEVEL_WARNING, "trx_x509_dn_gets() failed: %s", error);
		trx_free(error);
		gnutls_x509_crt_deinit(peer_cert);
		return FAIL;
	}

	if (0 != (res = gnutls_x509_crt_get_subject(peer_cert, &dn)))
	{
		treegix_log(LOG_LEVEL_WARNING, "gnutls_x509_crt_get_subject() failed: %d %s", res,
				gnutls_strerror(res));
		gnutls_x509_crt_deinit(peer_cert);
		return FAIL;
	}

	if (SUCCEED != trx_x509_dn_gets(dn, attr->subject, sizeof(attr->subject), &error))
	{
		treegix_log(LOG_LEVEL_WARNING, "trx_x509_dn_gets() failed: %s", error);
		trx_free(error);
		gnutls_x509_crt_deinit(peer_cert);
		return FAIL;
	}

	gnutls_x509_crt_deinit(peer_cert);
#elif defined(HAVE_OPENSSL)
	if (NULL == (peer_cert = SSL_get_peer_certificate(s->tls_ctx->ctx)))
	{
		treegix_log(LOG_LEVEL_WARNING, "no peer certificate, SSL_get_peer_certificate() returned NULL");
		return FAIL;
	}

	if (SUCCEED != trx_x509_dn_gets(X509_get_issuer_name(peer_cert), attr->issuer, sizeof(attr->issuer), &error))
	{
		treegix_log(LOG_LEVEL_WARNING, "error while getting issuer name: \"%s\"", error);
		trx_free(error);
		X509_free(peer_cert);
		return FAIL;
	}

	if (SUCCEED != trx_x509_dn_gets(X509_get_subject_name(peer_cert), attr->subject, sizeof(attr->subject), &error))
	{
		treegix_log(LOG_LEVEL_WARNING, "error while getting subject name: \"%s\"", error);
		trx_free(error);
		X509_free(peer_cert);
		return FAIL;
	}

	X509_free(peer_cert);
#endif

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tls_get_attr_psk                                             *
 *                                                                            *
 * Purpose: get PSK attributes from the context of established connection     *
 *                                                                            *
 * Comments:                                                                  *
 *     This function can be used only on server-side of TLS connection.       *
 *     GnuTLS makes it asymmetric - see documentation for                     *
 *     gnutls_psk_server_get_username() and gnutls_psk_client_get_hint()      *
 *     (the latter function is not used in Treegix).                           *
 *     Implementation for OpenSSL is server-side only, too.                   *
 *                                                                            *
 ******************************************************************************/
#if defined(HAVE_POLARSSL)
int	trx_tls_get_attr_psk(const trx_socket_t *s, trx_tls_conn_attr_t *attr)
{
	attr->psk_identity = (char *)s->tls_ctx->ctx->psk_identity;
	attr->psk_identity_len = s->tls_ctx->ctx->psk_identity_len;
	return SUCCEED;
}
#elif defined(HAVE_GNUTLS)
int	trx_tls_get_attr_psk(const trx_socket_t *s, trx_tls_conn_attr_t *attr)
{
	if (NULL == (attr->psk_identity = gnutls_psk_server_get_username(s->tls_ctx->ctx)))
		return FAIL;

	attr->psk_identity_len = strlen(attr->psk_identity);
	return SUCCEED;
}
#elif defined(HAVE_OPENSSL) && defined(HAVE_OPENSSL_WITH_PSK)
int	trx_tls_get_attr_psk(const trx_socket_t *s, trx_tls_conn_attr_t *attr)
{
	TRX_UNUSED(s);

	/* SSL_get_psk_identity() is not used here. It works with TLS 1.2, */
	/* but returns NULL with TLS 1.3 in OpenSSL 1.1.1 */
	if ('\0' == incoming_connection_psk_id[0])
		return FAIL;

	attr->psk_identity = incoming_connection_psk_id;
	attr->psk_identity_len = strlen(attr->psk_identity);
	return SUCCEED;
}
#endif

#if defined(_WINDOWS)
/******************************************************************************
 *                                                                            *
 * Function: trx_tls_pass_vars                                                *
 *                                                                            *
 * Purpose: pass some TLS variables from one thread to other                  *
 *                                                                            *
 * Comments: used in Treegix sender on MS Windows                              *
 *                                                                            *
 ******************************************************************************/
void	trx_tls_pass_vars(TRX_THREAD_SENDVAL_TLS_ARGS *args)
{
#if defined(HAVE_POLARSSL)
	args->my_psk = my_psk;
	args->my_psk_len = my_psk_len;
	args->my_psk_identity = my_psk_identity;
	args->my_psk_identity_len = my_psk_identity_len;
	args->ca_cert = ca_cert;
	args->crl = crl;
	args->my_cert = my_cert;
	args->my_priv_key = my_priv_key;
	args->entropy = entropy;
	args->ctr_drbg = ctr_drbg;
	args->ciphersuites_cert = ciphersuites_cert;
	args->ciphersuites_psk = ciphersuites_psk;
#elif defined(HAVE_GNUTLS)
	args->my_cert_creds = my_cert_creds;
	args->my_psk_client_creds = my_psk_client_creds;
	args->ciphersuites_cert = ciphersuites_cert;
	args->ciphersuites_psk = ciphersuites_psk;
#elif defined(HAVE_OPENSSL)
	args->ctx_cert = ctx_cert;
#if defined(HAVE_OPENSSL_WITH_PSK)
	args->ctx_psk = ctx_psk;
	args->psk_identity_for_cb = psk_identity_for_cb;
	args->psk_identity_len_for_cb = psk_identity_len_for_cb;
	args->psk_for_cb = psk_for_cb;
	args->psk_len_for_cb = psk_len_for_cb;
#endif
#endif	/* defined(HAVE_OPENSSL) */
}

/******************************************************************************
 *                                                                            *
 * Function: trx_tls_take_vars                                                *
 *                                                                            *
 * Purpose: pass some TLS variables from one thread to other                  *
 *                                                                            *
 * Comments: used in Treegix sender on MS Windows                              *
 *                                                                            *
 ******************************************************************************/
void	trx_tls_take_vars(TRX_THREAD_SENDVAL_TLS_ARGS *args)
{
#if defined(HAVE_POLARSSL)
	my_psk = args->my_psk;
	my_psk_len = args->my_psk_len;
	my_psk_identity = args->my_psk_identity;
	my_psk_identity_len = args->my_psk_identity_len;
	ca_cert = args->ca_cert;
	crl = args->crl;
	my_cert = args->my_cert;
	my_priv_key = args->my_priv_key;
	entropy = args->entropy;
	ctr_drbg = args->ctr_drbg;
	ciphersuites_cert = args->ciphersuites_cert;
	ciphersuites_psk = args->ciphersuites_psk;
#elif defined(HAVE_GNUTLS)
	my_cert_creds = args->my_cert_creds;
	my_psk_client_creds = args->my_psk_client_creds;
	ciphersuites_cert = args->ciphersuites_cert;
	ciphersuites_psk = args->ciphersuites_psk;
#elif defined(HAVE_OPENSSL)
	ctx_cert = args->ctx_cert;
#if defined(HAVE_OPENSSL_WITH_PSK)
	ctx_psk = args->ctx_psk;
	psk_identity_for_cb = args->psk_identity_for_cb;
	psk_identity_len_for_cb = args->psk_identity_len_for_cb;
	psk_for_cb = args->psk_for_cb;
	psk_len_for_cb = args->psk_len_for_cb;
#endif
#endif	/* defined(HAVE_OPENSSL) */
}
#endif

unsigned int	trx_tls_get_psk_usage(void)
{
	return	psk_usage;
}
#endif
