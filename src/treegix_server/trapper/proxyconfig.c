

#include "common.h"
#include "db.h"
#include "log.h"
#include "proxy.h"

#include "proxyconfig.h"
#include "../../libs/trxcrypto/tls_tcp_active.h"

/******************************************************************************
 *                                                                            *
 * Function: send_proxyconfig                                                 *
 *                                                                            *
 * Purpose: send configuration tables to the proxy from server                *
 *          (for active proxies)                                              *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
void	send_proxyconfig(trx_socket_t *sock, struct trx_json_parse *jp)
{
	char		*error = NULL;
	struct trx_json	j;
	DC_PROXY	proxy;
	int		flags = TRX_TCP_PROTOCOL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != get_active_proxy_from_request(jp, &proxy, &error))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot parse proxy configuration data request from active proxy at"
				" \"%s\": %s", sock->peer, error);
		goto out;
	}

	if (SUCCEED != trx_proxy_check_permissions(&proxy, sock, &error))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot accept connection from proxy \"%s\" at \"%s\", allowed address:"
				" \"%s\": %s", proxy.host, sock->peer, proxy.proxy_address, error);
		goto out;
	}

	trx_update_proxy_data(&proxy, trx_get_proxy_protocol_version(jp), time(NULL),
			(0 != (sock->protocol & TRX_TCP_COMPRESS) ? 1 : 0));

	if (0 != proxy.auto_compress)
		flags |= TRX_TCP_COMPRESS;

	trx_json_init(&j, TRX_JSON_STAT_BUF_LEN);

	if (SUCCEED != get_proxyconfig_data(proxy.hostid, &j, &error))
	{
		trx_send_response_ext(sock, FAIL, error, NULL, flags, CONFIG_TIMEOUT);
		treegix_log(LOG_LEVEL_WARNING, "cannot collect configuration data for proxy \"%s\" at \"%s\": %s",
				proxy.host, sock->peer, error);
		goto clean;
	}

	treegix_log(LOG_LEVEL_WARNING, "sending configuration data to proxy \"%s\" at \"%s\", datalen " TRX_FS_SIZE_T,
			proxy.host, sock->peer, (trx_fs_size_t)j.buffer_size);
	treegix_log(LOG_LEVEL_DEBUG, "%s", j.buffer);

	if (SUCCEED != trx_tcp_send_ext(sock, j.buffer, strlen(j.buffer), flags, CONFIG_TRAPPER_TIMEOUT))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot send configuration data to proxy \"%s\" at \"%s\": %s",
				proxy.host, sock->peer, trx_socket_strerror());
	}
clean:
	trx_json_free(&j);
out:
	trx_free(error);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: recv_proxyconfig                                                 *
 *                                                                            *
 * Purpose: receive configuration tables from server (passive proxies)        *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
void	recv_proxyconfig(trx_socket_t *sock, struct trx_json_parse *jp)
{
	struct trx_json_parse	jp_data;
	int			ret;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != (ret = trx_json_brackets_by_name(jp, TRX_PROTO_TAG_DATA, &jp_data)))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot parse proxy configuration data received from server at"
				" \"%s\": %s", sock->peer, trx_json_strerror());
		trx_send_proxy_response(sock, ret, trx_json_strerror(), CONFIG_TIMEOUT);
		goto out;
	}

	if (SUCCEED != check_access_passive_proxy(sock, TRX_SEND_RESPONSE, "configuration update"))
		goto out;

	process_proxyconfig(&jp_data);
	trx_send_proxy_response(sock, ret, NULL, CONFIG_TIMEOUT);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}
