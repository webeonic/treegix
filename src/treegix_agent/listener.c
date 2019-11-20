

#include "common.h"
#include "listener.h"

#include "comms.h"
#include "cfg.h"
#include "trxconf.h"
#include "stats.h"
#include "sysinfo.h"
#include "log.h"

extern unsigned char			program_type;
extern TRX_THREAD_LOCAL unsigned char	process_type;
extern TRX_THREAD_LOCAL int		server_num, process_num;

#if defined(TREEGIX_SERVICE)
#	include "service.h"
#elif defined(TREEGIX_DAEMON)
#	include "daemon.h"
#endif

#include "../libs/trxcrypto/tls.h"
#include "../libs/trxcrypto/tls_tcp_active.h"

static void	process_listener(trx_socket_t *s)
{
	AGENT_RESULT	result;
	char		**value = NULL;
	int		ret;

	if (SUCCEED == (ret = trx_tcp_recv_to(s, CONFIG_TIMEOUT)))
	{
		trx_rtrim(s->buffer, "\r\n");

		treegix_log(LOG_LEVEL_DEBUG, "Requested [%s]", s->buffer);

		init_result(&result);

		if (SUCCEED == process(s->buffer, PROCESS_WITH_ALIAS, &result))
		{
			if (NULL != (value = GET_TEXT_RESULT(&result)))
			{
				treegix_log(LOG_LEVEL_DEBUG, "Sending back [%s]", *value);
				ret = trx_tcp_send_to(s, *value, CONFIG_TIMEOUT);
			}
		}
		else
		{
			value = GET_MSG_RESULT(&result);

			if (NULL != value)
			{
				static char	*buffer = NULL;
				static size_t	buffer_alloc = 256;
				size_t		buffer_offset = 0;

				treegix_log(LOG_LEVEL_DEBUG, "Sending back [" TRX_NOTSUPPORTED ": %s]", *value);

				if (NULL == buffer)
					buffer = (char *)trx_malloc(buffer, buffer_alloc);

				trx_strncpy_alloc(&buffer, &buffer_alloc, &buffer_offset,
						TRX_NOTSUPPORTED, TRX_CONST_STRLEN(TRX_NOTSUPPORTED));
				buffer_offset++;
				trx_strcpy_alloc(&buffer, &buffer_alloc, &buffer_offset, *value);

				ret = trx_tcp_send_bytes_to(s, buffer, buffer_offset, CONFIG_TIMEOUT);
			}
			else
			{
				treegix_log(LOG_LEVEL_DEBUG, "Sending back [" TRX_NOTSUPPORTED "]");

				ret = trx_tcp_send_to(s, TRX_NOTSUPPORTED, CONFIG_TIMEOUT);
			}
		}

		free_result(&result);
	}

	if (FAIL == ret)
		treegix_log(LOG_LEVEL_DEBUG, "Process listener error: %s", trx_socket_strerror());
}

TRX_THREAD_ENTRY(listener_thread, args)
{
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	char		*msg = NULL;
#endif
	int		ret;
	trx_socket_t	s;

	assert(args);
	assert(((trx_thread_args_t *)args)->args);

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	memcpy(&s, (trx_socket_t *)((trx_thread_args_t *)args)->args, sizeof(trx_socket_t));

	trx_free(args);

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	trx_tls_init_child();
#endif
	while (TRX_IS_RUNNING())
	{
		trx_setproctitle("listener #%d [waiting for connection]", process_num);
		ret = trx_tcp_accept(&s, configured_tls_accept_modes);
		trx_update_env(trx_time());

		if (SUCCEED == ret)
		{
			trx_setproctitle("listener #%d [processing request]", process_num);

			if ('\0' != *CONFIG_HOSTS_ALLOWED &&
					SUCCEED == (ret = trx_tcp_check_allowed_peers(&s, CONFIG_HOSTS_ALLOWED)))
			{
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
				if (TRX_TCP_SEC_TLS_CERT != s.connection_type ||
						SUCCEED == (ret = trx_check_server_issuer_subject(&s, &msg)))
#endif
				{
					process_listener(&s);
				}
			}

			trx_tcp_unaccept(&s);
		}

		if (SUCCEED == ret || EINTR == trx_socket_last_error())
			continue;

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		if (NULL != msg)
		{
			treegix_log(LOG_LEVEL_WARNING, "failed to accept an incoming connection: %s", msg);
			trx_free(msg);
		}
		else
#endif
		{
			treegix_log(LOG_LEVEL_WARNING, "failed to accept an incoming connection: %s",
					trx_socket_strerror());
		}

		if (TRX_IS_RUNNING())
			trx_sleep(1);
	}

#ifdef _WINDOWS
	TRX_DO_EXIT();

	trx_thread_exit(EXIT_SUCCESS);
#else
	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);
#endif
}
