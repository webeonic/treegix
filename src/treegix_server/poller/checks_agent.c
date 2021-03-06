

#include "common.h"
#include "comms.h"
#include "log.h"
#include "../../libs/trxcrypto/tls_tcp_active.h"

#include "checks_agent.h"

#if !(defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL))
extern unsigned char	program_type;
#endif

/******************************************************************************
 *                                                                            *
 * Function: get_value_agent                                                  *
 *                                                                            *
 * Purpose: retrieve data from Treegix agent                                   *
 *                                                                            *
 * Parameters: item - item we are interested in                               *
 *                                                                            *
 * Return value: SUCCEED - data successfully retrieved and stored in result   *
 *                         and result_str (as string)                         *
 *               NETWORK_ERROR - network related error occurred               *
 *               NOTSUPPORTED - item not supported by the agent               *
 *               AGENT_ERROR - uncritical error on agent side occurred        *
 *               FAIL - otherwise                                             *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: error will contain error message                                 *
 *                                                                            *
 ******************************************************************************/
int	get_value_agent(DC_ITEM *item, AGENT_RESULT *result)
{
	trx_socket_t	s;
	char		*tls_arg1, *tls_arg2;
	int		ret = SUCCEED;
	ssize_t		received_len;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() host:'%s' addr:'%s' key:'%s' conn:'%s'", __func__, item->host.host,
			item->interface.addr, item->key, trx_tcp_connection_type_name(item->host.tls_connect));

	switch (item->host.tls_connect)
	{
		case TRX_TCP_SEC_UNENCRYPTED:
			tls_arg1 = NULL;
			tls_arg2 = NULL;
			break;
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		case TRX_TCP_SEC_TLS_CERT:
			tls_arg1 = item->host.tls_issuer;
			tls_arg2 = item->host.tls_subject;
			break;
		case TRX_TCP_SEC_TLS_PSK:
			tls_arg1 = item->host.tls_psk_identity;
			tls_arg2 = item->host.tls_psk;
			break;
#else
		case TRX_TCP_SEC_TLS_CERT:
		case TRX_TCP_SEC_TLS_PSK:
			SET_MSG_RESULT(result, trx_dsprintf(NULL, "A TLS connection is configured to be used with agent"
					" but support for TLS was not compiled into %s.",
					get_program_type_string(program_type)));
			ret = CONFIG_ERROR;
			goto out;
#endif
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid TLS connection parameters."));
			ret = CONFIG_ERROR;
			goto out;
	}

	if (SUCCEED == (ret = trx_tcp_connect(&s, CONFIG_SOURCE_IP, item->interface.addr, item->interface.port, 0,
			item->host.tls_connect, tls_arg1, tls_arg2)))
	{
		treegix_log(LOG_LEVEL_DEBUG, "Sending [%s]", item->key);

		if (SUCCEED != trx_tcp_send(&s, item->key))
			ret = NETWORK_ERROR;
		else if (FAIL != (received_len = trx_tcp_recv_ext(&s, 0)))
			ret = SUCCEED;
		else if (SUCCEED == trx_alarm_timed_out())
			ret = TIMEOUT_ERROR;
		else
			ret = NETWORK_ERROR;
	}
	else
		ret = NETWORK_ERROR;

	if (SUCCEED == ret)
	{
		treegix_log(LOG_LEVEL_DEBUG, "get value from agent result: '%s'", s.buffer);

		if (0 == strcmp(s.buffer, TRX_NOTSUPPORTED))
		{
			/* 'TRX_NOTSUPPORTED\0<error message>' */
			if (sizeof(TRX_NOTSUPPORTED) < s.read_bytes)
				SET_MSG_RESULT(result, trx_dsprintf(NULL, "%s", s.buffer + sizeof(TRX_NOTSUPPORTED)));
			else
				SET_MSG_RESULT(result, trx_strdup(NULL, "Not supported by Treegix Agent"));

			ret = NOTSUPPORTED;
		}
		else if (0 == strcmp(s.buffer, TRX_ERROR))
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Treegix Agent non-critical error"));
			ret = AGENT_ERROR;
		}
		else if (0 == received_len)
		{
			SET_MSG_RESULT(result, trx_dsprintf(NULL, "Received empty response from Treegix Agent at [%s]."
					" Assuming that agent dropped connection because of access permissions.",
					item->interface.addr));
			ret = NETWORK_ERROR;
		}
		else
			set_result_type(result, ITEM_VALUE_TYPE_TEXT, s.buffer);
	}
	else
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Get value from agent failed: %s", trx_socket_strerror()));

	trx_tcp_close(&s);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}
