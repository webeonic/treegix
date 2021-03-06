

#include "common.h"

#include "cfg.h"
#include "db.h"
#include "log.h"
#include "trxjson.h"

#include "comms.h"
#include "servercomms.h"
#include "daemon.h"

extern unsigned int	configured_tls_connect_mode;

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
extern char	*CONFIG_TLS_SERVER_CERT_ISSUER;
extern char	*CONFIG_TLS_SERVER_CERT_SUBJECT;
extern char	*CONFIG_TLS_PSK_IDENTITY;
#endif

int	connect_to_server(trx_socket_t *sock, int timeout, int retry_interval)
{
	int	res, lastlogtime, now;
	char	*tls_arg1, *tls_arg2;

	treegix_log(LOG_LEVEL_DEBUG, "In connect_to_server() [%s]:%d [timeout:%d]",
			CONFIG_SERVER, CONFIG_SERVER_PORT, timeout);

	switch (configured_tls_connect_mode)
	{
		case TRX_TCP_SEC_UNENCRYPTED:
			tls_arg1 = NULL;
			tls_arg2 = NULL;
			break;
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		case TRX_TCP_SEC_TLS_CERT:
			tls_arg1 = CONFIG_TLS_SERVER_CERT_ISSUER;
			tls_arg2 = CONFIG_TLS_SERVER_CERT_SUBJECT;
			break;
		case TRX_TCP_SEC_TLS_PSK:
			tls_arg1 = CONFIG_TLS_PSK_IDENTITY;
			tls_arg2 = NULL;	/* trx_tls_connect() will find PSK */
			break;
#endif
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
	}

	if (FAIL == (res = trx_tcp_connect(sock, CONFIG_SOURCE_IP, CONFIG_SERVER, CONFIG_SERVER_PORT, timeout,
			configured_tls_connect_mode, tls_arg1, tls_arg2)))
	{
		if (0 == retry_interval)
		{
			treegix_log(LOG_LEVEL_WARNING, "Unable to connect to the server [%s]:%d [%s]",
					CONFIG_SERVER, CONFIG_SERVER_PORT, trx_socket_strerror());
		}
		else
		{
			treegix_log(LOG_LEVEL_WARNING, "Unable to connect to the server [%s]:%d [%s]. Will retry every"
					" %d second(s)", CONFIG_SERVER, CONFIG_SERVER_PORT, trx_socket_strerror(),
					retry_interval);

			lastlogtime = (int)time(NULL);

			while (TRX_IS_RUNNING() && FAIL == (res = trx_tcp_connect(sock, CONFIG_SOURCE_IP,
					CONFIG_SERVER, CONFIG_SERVER_PORT, timeout, configured_tls_connect_mode,
					tls_arg1, tls_arg2)))
			{
				now = (int)time(NULL);

				if (LOG_ENTRY_INTERVAL_DELAY <= now - lastlogtime)
				{
					treegix_log(LOG_LEVEL_WARNING, "Still unable to connect...");
					lastlogtime = now;
				}

				sleep(retry_interval);
			}

			if (FAIL != res)
				treegix_log(LOG_LEVEL_WARNING, "Connection restored.");
		}
	}

	return res;
}

void	disconnect_server(trx_socket_t *sock)
{
	trx_tcp_close(sock);
}

/******************************************************************************
 *                                                                            *
 * Function: get_data_from_server                                             *
 *                                                                            *
 * Purpose: get configuration and other data from server                      *
 *                                                                            *
 * Return value: SUCCEED - processed successfully                             *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 ******************************************************************************/
int	get_data_from_server(trx_socket_t *sock, const char *request, char **error)
{
	int		ret = FAIL;
	struct trx_json	j;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() request:'%s'", __func__, request);

	trx_json_init(&j, 128);
	trx_json_addstring(&j, "request", request, TRX_JSON_TYPE_STRING);
	trx_json_addstring(&j, "host", CONFIG_HOSTNAME, TRX_JSON_TYPE_STRING);
	trx_json_addstring(&j, TRX_PROTO_TAG_VERSION, TREEGIX_VERSION, TRX_JSON_TYPE_STRING);

	if (SUCCEED != trx_tcp_send_ext(sock, j.buffer, strlen(j.buffer), TRX_TCP_PROTOCOL | TRX_TCP_COMPRESS, 0))
	{
		*error = trx_strdup(*error, trx_socket_strerror());
		goto exit;
	}

	if (SUCCEED != trx_tcp_recv(sock))
	{
		*error = trx_strdup(*error, trx_socket_strerror());
		goto exit;
	}

	treegix_log(LOG_LEVEL_DEBUG, "Received [%s] from server", sock->buffer);

	ret = SUCCEED;
exit:
	trx_json_free(&j);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: put_data_to_server                                               *
 *                                                                            *
 * Purpose: send data to server                                               *
 *                                                                            *
 * Return value: SUCCEED - processed successfully                             *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 ******************************************************************************/
int	put_data_to_server(trx_socket_t *sock, struct trx_json *j, char **error)
{
	int	ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() datalen:" TRX_FS_SIZE_T, __func__, (trx_fs_size_t)j->buffer_size);

	if (SUCCEED != trx_tcp_send_ext(sock, j->buffer, strlen(j->buffer), TRX_TCP_PROTOCOL | TRX_TCP_COMPRESS, 0))
	{
		*error = trx_strdup(*error, trx_socket_strerror());
		goto out;
	}

	if (SUCCEED != trx_recv_response(sock, 0, error))
		goto out;

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}
