

#include "common.h"
#include "../treegix_server/alerter/alerter_protocol.h"

zbx_uint32_t	zbx_alerter_serialize_alert_send(unsigned char **data, zbx_uint64_t mediatypeid, unsigned char type,
		const char *smtp_server, const char *smtp_helo, const char *smtp_email, const char *exec_path,
		const char *gsm_modem, const char *username, const char *passwd, unsigned short smtp_port,
		unsigned char smtp_security, unsigned char smtp_verify_peer, unsigned char smtp_verify_host,
		unsigned char smtp_authentication, const char *exec_params, int maxsessions, int maxattempts,
		const char *attempt_interval, unsigned char content_type, const char *script, const char *timeout,
		const char *sendto, const char *subject, const char *message, const char *params)
{
	TRX_UNUSED(data);
	TRX_UNUSED(mediatypeid);
	TRX_UNUSED(type);
	TRX_UNUSED(smtp_server);
	TRX_UNUSED(smtp_helo);
	TRX_UNUSED(smtp_email);
	TRX_UNUSED(exec_path);
	TRX_UNUSED(gsm_modem);
	TRX_UNUSED(username);
	TRX_UNUSED(passwd);
	TRX_UNUSED(smtp_port);
	TRX_UNUSED(smtp_security);
	TRX_UNUSED(smtp_verify_peer);
	TRX_UNUSED(smtp_verify_host);
	TRX_UNUSED(smtp_authentication);
	TRX_UNUSED(exec_params);
	TRX_UNUSED(maxsessions);
	TRX_UNUSED(maxattempts);
	TRX_UNUSED(attempt_interval);
	TRX_UNUSED(content_type);
	TRX_UNUSED(script);
	TRX_UNUSED(timeout);
	TRX_UNUSED(sendto);
	TRX_UNUSED(subject);
	TRX_UNUSED(message);
	TRX_UNUSED(params);

	THIS_SHOULD_NEVER_HAPPEN;

	return 0;
}

void	zbx_alerter_deserialize_result(const unsigned char *data, char **value, int *errcode, char **errmsg)
{
	TRX_UNUSED(value);
	TRX_UNUSED(data);
	TRX_UNUSED(errcode);
	TRX_UNUSED(errmsg);

	THIS_SHOULD_NEVER_HAPPEN;
}
