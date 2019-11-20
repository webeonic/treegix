

#include "common.h"

#include "log.h"
#include "trxserialize.h"

#include "alerter_protocol.h"

void	trx_am_db_mediatype_clear(trx_am_db_mediatype_t *mediatype)
{
	trx_free(mediatype->smtp_server);
	trx_free(mediatype->smtp_helo);
	trx_free(mediatype->smtp_email);
	trx_free(mediatype->exec_path);
	trx_free(mediatype->exec_params);
	trx_free(mediatype->gsm_modem);
	trx_free(mediatype->username);
	trx_free(mediatype->passwd);
	trx_free(mediatype->script);
	trx_free(mediatype->attempt_interval);
	trx_free(mediatype->timeout);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_am_db_alert_free                                             *
 *                                                                            *
 * Purpose: frees the alert object                                            *
 *                                                                            *
 * Parameters: alert - [IN] the alert object                                  *
 *                                                                            *
 ******************************************************************************/
void	trx_am_db_alert_free(trx_am_db_alert_t *alert)
{
	trx_free(alert->sendto);
	trx_free(alert->subject);
	trx_free(alert->message);
	trx_free(alert->params);
	trx_free(alert);
}

void	trx_am_media_clear(trx_am_media_t *media)
{
	trx_free(media->sendto);
}

void	trx_am_media_free(trx_am_media_t *media)
{
	trx_am_media_clear(media);
	trx_free(media);
}

trx_uint32_t	trx_alerter_serialize_result(unsigned char **data, const char *value, int errcode, const char *error)
{
	unsigned char	*ptr;
	trx_uint32_t	data_len = 0, value_len, error_len;

	trx_serialize_prepare_str(data_len, value);
	trx_serialize_prepare_value(data_len, errcode);
	trx_serialize_prepare_str(data_len, error);

	*data = (unsigned char *)trx_malloc(NULL, data_len);

	ptr = *data;
	ptr += trx_serialize_str(ptr, value, value_len);
	ptr += trx_serialize_value(ptr, errcode);
	(void)trx_serialize_str(ptr, error, error_len);

	return data_len;
}

void	trx_alerter_deserialize_result(const unsigned char *data, char **value, int *errcode, char **error)
{
	trx_uint32_t	len;

	data += trx_deserialize_str(data, value, len);
	data += trx_deserialize_value(data, errcode);
	(void)trx_deserialize_str(data, error, len);
}

trx_uint32_t	trx_alerter_serialize_email(unsigned char **data, trx_uint64_t alertid, const char *sendto,
		const char *subject, const char *message, const char *smtp_server, unsigned short smtp_port,
		const char *smtp_helo, const char *smtp_email, unsigned char smtp_security,
		unsigned char smtp_verify_peer, unsigned char smtp_verify_host, unsigned char smtp_authentication,
		const char *username, const char *password, unsigned char content_type)
{
	unsigned char	*ptr;
	trx_uint32_t	data_len = 0, sendto_len, subject_len, message_len, smtp_server_len, smtp_helo_len,
			smtp_email_len, username_len, password_len;

	trx_serialize_prepare_value(data_len, alertid);
	trx_serialize_prepare_str(data_len, sendto);
	trx_serialize_prepare_str(data_len, subject);
	trx_serialize_prepare_str(data_len, message);
	trx_serialize_prepare_str(data_len, smtp_server);
	trx_serialize_prepare_value(data_len, smtp_port);
	trx_serialize_prepare_str(data_len, smtp_helo);
	trx_serialize_prepare_str(data_len, smtp_email);
	trx_serialize_prepare_value(data_len, smtp_security);
	trx_serialize_prepare_value(data_len, smtp_verify_peer);
	trx_serialize_prepare_value(data_len, smtp_verify_host);
	trx_serialize_prepare_value(data_len, smtp_authentication);
	trx_serialize_prepare_str(data_len, username);
	trx_serialize_prepare_str(data_len, password);
	trx_serialize_prepare_value(data_len, content_type);

	*data = (unsigned char *)trx_malloc(NULL, data_len);

	ptr = *data;
	ptr += trx_serialize_value(ptr, alertid);
	ptr += trx_serialize_str(ptr, sendto, sendto_len);
	ptr += trx_serialize_str(ptr, subject, subject_len);
	ptr += trx_serialize_str(ptr, message, message_len);
	ptr += trx_serialize_str(ptr, smtp_server, smtp_server_len);
	ptr += trx_serialize_value(ptr, smtp_port);
	ptr += trx_serialize_str(ptr, smtp_helo, smtp_helo_len);
	ptr += trx_serialize_str(ptr, smtp_email, smtp_email_len);
	ptr += trx_serialize_value(ptr, smtp_security);
	ptr += trx_serialize_value(ptr, smtp_verify_peer);
	ptr += trx_serialize_value(ptr, smtp_verify_host);
	ptr += trx_serialize_value(ptr, smtp_authentication);
	ptr += trx_serialize_str(ptr, username, username_len);
	ptr += trx_serialize_str(ptr, password, password_len);
	(void)trx_serialize_value(ptr, content_type);

	return data_len;
}

void	trx_alerter_deserialize_email(const unsigned char *data, trx_uint64_t *alertid, char **sendto, char **subject,
		char **message, char **smtp_server, unsigned short *smtp_port, char **smtp_helo, char **smtp_email,
		unsigned char *smtp_security, unsigned char *smtp_verify_peer, unsigned char *smtp_verify_host,
		unsigned char *smtp_authentication, char **username, char **password, unsigned char *content_type)
{
	trx_uint32_t	len;

	data += trx_deserialize_value(data, alertid);
	data += trx_deserialize_str(data, sendto, len);
	data += trx_deserialize_str(data, subject, len);
	data += trx_deserialize_str(data, message, len);
	data += trx_deserialize_str(data, smtp_server, len);
	data += trx_deserialize_value(data, smtp_port);
	data += trx_deserialize_str(data, smtp_helo, len);
	data += trx_deserialize_str(data, smtp_email, len);
	data += trx_deserialize_value(data, smtp_security);
	data += trx_deserialize_value(data, smtp_verify_peer);
	data += trx_deserialize_value(data, smtp_verify_host);
	data += trx_deserialize_value(data, smtp_authentication);
	data += trx_deserialize_str(data, username, len);
	data += trx_deserialize_str(data, password, len);
	(void)trx_deserialize_value(data, content_type);
}

trx_uint32_t	trx_alerter_serialize_sms(unsigned char **data, trx_uint64_t alertid,  const char *sendto,
		const char *message, const char *gsm_modem)
{
	unsigned char	*ptr;
	trx_uint32_t	data_len = 0, sendto_len, gsm_modem_len, message_len;

	trx_serialize_prepare_value(data_len, alertid);
	trx_serialize_prepare_str(data_len, sendto);
	trx_serialize_prepare_str(data_len, message);
	trx_serialize_prepare_str(data_len, gsm_modem);

	*data = (unsigned char *)trx_malloc(NULL, data_len);

	ptr = *data;
	ptr += trx_serialize_value(ptr, alertid);
	ptr += trx_serialize_str(ptr, sendto, sendto_len);
	ptr += trx_serialize_str(ptr, message, message_len);
	(void)trx_serialize_str(ptr, gsm_modem, gsm_modem_len);

	return data_len;
}

void	trx_alerter_deserialize_sms(const unsigned char *data, trx_uint64_t *alertid, char **sendto, char **message,
		char **gsm_modem)
{
	trx_uint32_t	len;

	data += trx_deserialize_value(data, alertid);
	data += trx_deserialize_str(data, sendto, len);
	data += trx_deserialize_str(data, message, len);
	(void)trx_deserialize_str(data, gsm_modem, len);
}

trx_uint32_t	trx_alerter_serialize_exec(unsigned char **data, trx_uint64_t alertid, const char *command)
{
	unsigned char	*ptr;
	trx_uint32_t	data_len = 0, command_len;

	trx_serialize_prepare_value(data_len, alertid);
	trx_serialize_prepare_str(data_len, command);

	*data = (unsigned char *)trx_malloc(NULL, data_len);

	ptr = *data;
	ptr += trx_serialize_value(ptr, alertid);
	(void)trx_serialize_str(ptr, command, command_len);

	return data_len;
}

void	trx_alerter_deserialize_exec(const unsigned char *data, trx_uint64_t *alertid, char **command)
{
	trx_uint32_t	len;

	data += trx_deserialize_value(data, alertid);
	(void)trx_deserialize_str(data, command, len);
}

static void	alerter_serialize_mediatype(unsigned char **data, trx_uint32_t *data_alloc, trx_uint32_t *data_offset,
		trx_uint64_t mediatypeid, unsigned char type, const char *smtp_server, const char *smtp_helo,
		const char *smtp_email, const char *exec_path, const char *gsm_modem, const char *username,
		const char *passwd, unsigned short smtp_port, unsigned char smtp_security,
		unsigned char smtp_verify_peer, unsigned char smtp_verify_host, unsigned char smtp_authentication,
		const char *exec_params, int maxsessions, int maxattempts, const char *attempt_interval,
		unsigned char content_type, const char *script, const char *timeout)
{
	trx_uint32_t	data_len = 0, smtp_server_len, smtp_helo_len, smtp_email_len, exec_path_len, gsm_modem_len,
			username_len, passwd_len, exec_params_len, script_len, attempt_interval_len, timeout_len;
	unsigned char	*ptr;

	trx_serialize_prepare_value(data_len, mediatypeid);
	trx_serialize_prepare_value(data_len, type);
	trx_serialize_prepare_str_len(data_len, smtp_server, smtp_server_len);
	trx_serialize_prepare_str_len(data_len, smtp_helo, smtp_helo_len);
	trx_serialize_prepare_str_len(data_len, smtp_email, smtp_email_len);
	trx_serialize_prepare_str_len(data_len, exec_path, exec_path_len);
	trx_serialize_prepare_str_len(data_len, gsm_modem, gsm_modem_len);
	trx_serialize_prepare_str_len(data_len, username, username_len);
	trx_serialize_prepare_str_len(data_len, passwd, passwd_len);
	trx_serialize_prepare_value(data_len, smtp_port);
	trx_serialize_prepare_value(data_len, smtp_security);
	trx_serialize_prepare_value(data_len, smtp_verify_peer);
	trx_serialize_prepare_value(data_len, smtp_verify_host);
	trx_serialize_prepare_value(data_len, smtp_authentication);
	trx_serialize_prepare_str_len(data_len, exec_params, exec_params_len);
	trx_serialize_prepare_value(data_len, maxsessions);
	trx_serialize_prepare_value(data_len, maxattempts);
	trx_serialize_prepare_str_len(data_len, attempt_interval, attempt_interval_len);
	trx_serialize_prepare_value(data_len, content_type);
	trx_serialize_prepare_str_len(data_len, script, script_len);
	trx_serialize_prepare_str_len(data_len, timeout, timeout_len);

	while (data_len > *data_alloc - *data_offset)
	{
		*data_alloc *= 2;
		*data = (unsigned char *)trx_realloc(*data, *data_alloc);
	}

	ptr = *data + *data_offset;
	ptr += trx_serialize_value(ptr, mediatypeid);
	ptr += trx_serialize_value(ptr, type);
	ptr += trx_serialize_str(ptr, smtp_server, smtp_server_len);
	ptr += trx_serialize_str(ptr, smtp_helo, smtp_helo_len);
	ptr += trx_serialize_str(ptr, smtp_email, smtp_email_len);
	ptr += trx_serialize_str(ptr, exec_path, exec_path_len);
	ptr += trx_serialize_str(ptr, gsm_modem, gsm_modem_len);
	ptr += trx_serialize_str(ptr, username, username_len);
	ptr += trx_serialize_str(ptr, passwd, passwd_len);
	ptr += trx_serialize_value(ptr, smtp_port);
	ptr += trx_serialize_value(ptr, smtp_security);
	ptr += trx_serialize_value(ptr, smtp_verify_peer);
	ptr += trx_serialize_value(ptr, smtp_verify_host);
	ptr += trx_serialize_value(ptr, smtp_authentication);
	ptr += trx_serialize_str(ptr, exec_params, exec_params_len);
	ptr += trx_serialize_value(ptr, maxsessions);
	ptr += trx_serialize_value(ptr, maxattempts);
	ptr += trx_serialize_str(ptr, attempt_interval, attempt_interval_len);
	ptr += trx_serialize_value(ptr, content_type);
	ptr += trx_serialize_str(ptr, script, script_len);
	(void)trx_serialize_str(ptr, timeout, timeout_len);

	*data_offset += data_len;
}

static trx_uint32_t	alerter_deserialize_mediatype(const unsigned char *data, trx_uint64_t *mediatypeid,
		unsigned char *type, char **smtp_server, char **smtp_helo, char **smtp_email, char **exec_path,
		char **gsm_modem, char **username, char **passwd, unsigned short *smtp_port,
		unsigned char *smtp_security, unsigned char *smtp_verify_peer, unsigned char *smtp_verify_host,
		unsigned char *smtp_authentication, char **exec_params, int *maxsessions, int *maxattempts,
		char **attempt_interval, unsigned char *content_type, char **script, char **timeout)
{
	trx_uint32_t		len;
	const unsigned char	*start = data;

	data += trx_deserialize_value(data, mediatypeid);
	data += trx_deserialize_value(data, type);
	data += trx_deserialize_str(data, smtp_server, len);
	data += trx_deserialize_str(data, smtp_helo, len);
	data += trx_deserialize_str(data, smtp_email, len);
	data += trx_deserialize_str(data, exec_path, len);
	data += trx_deserialize_str(data, gsm_modem, len);
	data += trx_deserialize_str(data, username, len);
	data += trx_deserialize_str(data, passwd, len);
	data += trx_deserialize_value(data, smtp_port);
	data += trx_deserialize_value(data, smtp_security);
	data += trx_deserialize_value(data, smtp_verify_peer);
	data += trx_deserialize_value(data, smtp_verify_host);
	data += trx_deserialize_value(data, smtp_authentication);
	data += trx_deserialize_str(data, exec_params, len);
	data += trx_deserialize_value(data, maxsessions);
	data += trx_deserialize_value(data, maxattempts);
	data += trx_deserialize_str(data, attempt_interval, len);
	data += trx_deserialize_value(data, content_type);
	data += trx_deserialize_str(data, script, len);
	data += trx_deserialize_str(data, timeout, len);

	return data - start;
}

trx_uint32_t	trx_alerter_serialize_alert_send(unsigned char **data, trx_uint64_t mediatypeid, unsigned char type,
		const char *smtp_server, const char *smtp_helo, const char *smtp_email, const char *exec_path,
		const char *gsm_modem, const char *username, const char *passwd, unsigned short smtp_port,
		unsigned char smtp_security, unsigned char smtp_verify_peer, unsigned char smtp_verify_host,
		unsigned char smtp_authentication, const char *exec_params, int maxsessions, int maxattempts,
		const char *attempt_interval, unsigned char content_type, const char *script, const char *timeout,
		const char *sendto, const char *subject, const char *message, const char *params)
{
	unsigned char	*ptr;
	trx_uint32_t	data_len = 0, data_alloc = 1024, data_offset = 0, sendto_len, subject_len, message_len,
			params_len;

	*data = trx_malloc(0, data_alloc);
	alerter_serialize_mediatype(data, &data_alloc, &data_offset, mediatypeid, type, smtp_server, smtp_helo,
			smtp_email, exec_path, gsm_modem, username, passwd, smtp_port, smtp_security, smtp_verify_peer,
			smtp_verify_host, smtp_authentication, exec_params, maxsessions, maxattempts, attempt_interval,
			content_type, script, timeout);

	trx_serialize_prepare_str(data_len, sendto);
	trx_serialize_prepare_str(data_len, subject);
	trx_serialize_prepare_str(data_len, message);
	trx_serialize_prepare_str(data_len, params);

	if (data_alloc - data_offset < data_len)
	{
		data_alloc = data_offset + data_len;
		*data = (unsigned char *)trx_realloc(*data, data_alloc);
	}

	ptr = *data + data_offset;
	ptr += trx_serialize_str(ptr, sendto, sendto_len);
	ptr += trx_serialize_str(ptr, subject, subject_len);
	ptr += trx_serialize_str(ptr, message, message_len);
	(void)trx_serialize_str(ptr, params, params_len);

	return data_len + data_offset;
}

void	trx_alerter_deserialize_alert_send(const unsigned char *data, trx_uint64_t *mediatypeid,
		unsigned char *type, char **smtp_server, char **smtp_helo, char **smtp_email, char **exec_path,
		char **gsm_modem, char **username, char **passwd, unsigned short *smtp_port,
		unsigned char *smtp_security, unsigned char *smtp_verify_peer, unsigned char *smtp_verify_host,
		unsigned char *smtp_authentication, char **exec_params, int *maxsessions, int *maxattempts,
		char **attempt_interval, unsigned char *content_type, char **script, char **timeout,
		char **sendto, char **subject, char **message, char **params)
{
	trx_uint32_t	len;

	data += alerter_deserialize_mediatype(data, mediatypeid, type, smtp_server, smtp_helo,
			smtp_email, exec_path, gsm_modem, username, passwd, smtp_port, smtp_security, smtp_verify_peer,
			smtp_verify_host, smtp_authentication, exec_params, maxsessions, maxattempts, attempt_interval,
			content_type, script, timeout);

	data += trx_deserialize_str(data, sendto, len);
	data += trx_deserialize_str(data, subject, len);
	data += trx_deserialize_str(data, message, len);
	(void)trx_deserialize_str(data, params, len);
}

trx_uint32_t	trx_alerter_serialize_webhook(unsigned char **data, const char *script_bin, int script_sz,
		int timeout, const char *params)
{
	unsigned char	*ptr;
	trx_uint32_t	data_len = 0, params_len;

	data_len += script_sz + sizeof(trx_uint32_t);
	trx_serialize_prepare_value(data_len, script_sz);
	trx_serialize_prepare_value(data_len, timeout);
	trx_serialize_prepare_str(data_len, params);

	*data = (unsigned char *)trx_malloc(NULL, data_len);

	ptr = *data;
	ptr += trx_serialize_str(ptr, script_bin, script_sz);
	ptr += trx_serialize_value(ptr, script_sz);
	ptr += trx_serialize_value(ptr, timeout);
	(void)trx_serialize_str(ptr, params, params_len);

	return data_len;
}

void	trx_alerter_deserialize_webhook(const unsigned char *data, char **script_bin, int *script_sz, int *timeout,
		char **params)
{
	trx_uint32_t	len;

	data += trx_deserialize_str(data, script_bin, len);
	data += trx_deserialize_value(data, script_sz);
	data += trx_deserialize_value(data, timeout);
	(void)trx_deserialize_str(data, params, len);
}

trx_uint32_t	trx_alerter_serialize_mediatypes(unsigned char **data, trx_am_db_mediatype_t **mediatypes,
		int mediatypes_num)
{
	unsigned char	*ptr;
	int		i;
	trx_uint32_t	data_alloc = 1024, data_offset = 0;

	ptr = *data = (unsigned char *)trx_malloc(NULL, data_alloc);
	trx_serialize_prepare_value(data_offset, mediatypes_num);
	(void)trx_serialize_value(ptr, mediatypes_num);

	for (i = 0; i < mediatypes_num; i++)
	{
		trx_am_db_mediatype_t	*mt = mediatypes[i];

		alerter_serialize_mediatype(data, &data_alloc, &data_offset, mt->mediatypeid, mt->type, mt->smtp_server,
				mt->smtp_helo, mt->smtp_email, mt->exec_path, mt->gsm_modem, mt->username, mt->passwd,
				mt->smtp_port, mt->smtp_security, mt->smtp_verify_peer, mt->smtp_verify_host,
				mt->smtp_authentication, mt->exec_params, mt->maxsessions, mt->maxattempts,
				mt->attempt_interval, mt->content_type, mt->script, mt->timeout);
	}

	return data_offset;
}

void	trx_alerter_deserialize_mediatypes(const unsigned char *data, trx_am_db_mediatype_t ***mediatypes,
		int *mediatypes_num)
{
	int	i;

	data += trx_deserialize_value(data, mediatypes_num);
	*mediatypes = (trx_am_db_mediatype_t **)trx_malloc(NULL, *mediatypes_num * sizeof(trx_am_db_mediatype_t *));
	for (i = 0; i < *mediatypes_num; i++)
	{
		trx_am_db_mediatype_t	*mt;
		mt = (trx_am_db_mediatype_t *)trx_malloc(NULL, sizeof(trx_am_db_mediatype_t));

		data += alerter_deserialize_mediatype(data, &mt->mediatypeid, &mt->type, &mt->smtp_server,
				&mt->smtp_helo, &mt->smtp_email, &mt->exec_path, &mt->gsm_modem, &mt->username,
				&mt->passwd, &mt->smtp_port, &mt->smtp_security, &mt->smtp_verify_peer,
				&mt->smtp_verify_host, &mt->smtp_authentication, &mt->exec_params, &mt->maxsessions,
				&mt->maxattempts, &mt->attempt_interval, &mt->content_type, &mt->script, &mt->timeout);

		(*mediatypes)[i] = mt;
	}
}

trx_uint32_t	trx_alerter_serialize_alerts(unsigned char **data, trx_am_db_alert_t **alerts, int alerts_num)
{
	unsigned char	*ptr;
	int		i;
	trx_uint32_t	data_alloc = 1024, data_offset = 0;

	ptr = *data = (unsigned char *)trx_malloc(NULL, data_alloc);
	trx_serialize_prepare_value(data_offset, alerts_num);
	(void)trx_serialize_value(ptr, alerts_num);

	for (i = 0; i < alerts_num; i++)
	{
		trx_uint32_t		data_len = 0, sendto_len, subject_len, message_len, params_len;
		trx_am_db_alert_t	*alert = alerts[i];

		trx_serialize_prepare_value(data_len, alert->alertid);
		trx_serialize_prepare_value(data_len, alert->mediatypeid);
		trx_serialize_prepare_value(data_len, alert->eventid);
		trx_serialize_prepare_value(data_len, alert->source);
		trx_serialize_prepare_value(data_len, alert->object);
		trx_serialize_prepare_value(data_len, alert->objectid);
		trx_serialize_prepare_str_len(data_len, alert->sendto, sendto_len);
		trx_serialize_prepare_str_len(data_len, alert->subject, subject_len);
		trx_serialize_prepare_str_len(data_len, alert->message, message_len);
		trx_serialize_prepare_str_len(data_len, alert->params, params_len);
		trx_serialize_prepare_value(data_len, alert->status);
		trx_serialize_prepare_value(data_len, alert->retries);

		while (data_len > data_alloc - data_offset)
		{
			data_alloc *= 2;
			*data = (unsigned char *)trx_realloc(*data, data_alloc);
		}
		ptr = *data + data_offset;
		ptr += trx_serialize_value(ptr, alert->alertid);
		ptr += trx_serialize_value(ptr, alert->mediatypeid);
		ptr += trx_serialize_value(ptr, alert->eventid);
		ptr += trx_serialize_value(ptr, alert->source);
		ptr += trx_serialize_value(ptr, alert->object);
		ptr += trx_serialize_value(ptr, alert->objectid);
		ptr += trx_serialize_str(ptr, alert->sendto, sendto_len);
		ptr += trx_serialize_str(ptr, alert->subject, subject_len);
		ptr += trx_serialize_str(ptr, alert->message, message_len);
		ptr += trx_serialize_str(ptr, alert->params, params_len);
		ptr += trx_serialize_value(ptr, alert->status);
		(void)trx_serialize_value(ptr, alert->retries);

		data_offset += data_len;
	}

	return data_offset;
}

void	trx_alerter_deserialize_alerts(const unsigned char *data, trx_am_db_alert_t ***alerts, int *alerts_num)
{
	trx_uint32_t	len;
	int		i;

	data += trx_deserialize_value(data, alerts_num);
	*alerts = (trx_am_db_alert_t **)trx_malloc(NULL, *alerts_num * sizeof(trx_am_db_alert_t *));
	for (i = 0; i < *alerts_num; i++)
	{
		trx_am_db_alert_t	*alert;
		alert = (trx_am_db_alert_t *)trx_malloc(NULL, sizeof(trx_am_db_alert_t));

		data += trx_deserialize_value(data, &alert->alertid);
		data += trx_deserialize_value(data, &alert->mediatypeid);
		data += trx_deserialize_value(data, &alert->eventid);
		data += trx_deserialize_value(data, &alert->source);
		data += trx_deserialize_value(data, &alert->object);
		data += trx_deserialize_value(data, &alert->objectid);
		data += trx_deserialize_str(data, &alert->sendto, len);
		data += trx_deserialize_str(data, &alert->subject, len);
		data += trx_deserialize_str(data, &alert->message, len);
		data += trx_deserialize_str(data, &alert->params, len);
		data += trx_deserialize_value(data, &alert->status);
		data += trx_deserialize_value(data, &alert->retries);

		(*alerts)[i] = alert;
	}
}

trx_uint32_t	trx_alerter_serialize_medias(unsigned char **data, trx_am_media_t **medias, int medias_num)
{
	unsigned char	*ptr;
	int		i;
	trx_uint32_t	data_alloc = 1024, data_offset = 0;

	ptr = *data = (unsigned char *)trx_malloc(NULL, data_alloc);
	trx_serialize_prepare_value(data_offset, medias_num);
	(void)trx_serialize_value(ptr, medias_num);

	for (i = 0; i < medias_num; i++)
	{
		trx_uint32_t	data_len = 0, sendto_len;
		trx_am_media_t	*media = medias[i];

		trx_serialize_prepare_value(data_len, media->mediaid);
		trx_serialize_prepare_value(data_len, media->mediatypeid);
		trx_serialize_prepare_str_len(data_len, media->sendto, sendto_len);

		while (data_len > data_alloc - data_offset)
		{
			data_alloc *= 2;
			*data = (unsigned char *)trx_realloc(*data, data_alloc);
		}
		ptr = *data + data_offset;
		ptr += trx_serialize_value(ptr, media->mediaid);
		ptr += trx_serialize_value(ptr, media->mediatypeid);
		(void)trx_serialize_str(ptr, media->sendto, sendto_len);

		data_offset += data_len;
	}

	return data_offset;
}

void	trx_alerter_deserialize_medias(const unsigned char *data, trx_am_media_t ***medias, int *medias_num)
{
	trx_uint32_t	len;
	int		i;

	data += trx_deserialize_value(data, medias_num);
	*medias = (trx_am_media_t **)trx_malloc(NULL, *medias_num * sizeof(trx_am_media_t *));
	for (i = 0; i < *medias_num; i++)
	{
		trx_am_media_t	*media;
		media = (trx_am_media_t *)trx_malloc(NULL, sizeof(trx_am_media_t));

		data += trx_deserialize_value(data, &media->mediaid);
		data += trx_deserialize_value(data, &media->mediatypeid);
		data += trx_deserialize_str(data, &media->sendto, len);

		(*medias)[i] = media;
	}
}

trx_uint32_t	trx_alerter_serialize_results(unsigned char **data, trx_am_result_t **results, int results_num)
{
	unsigned char	*ptr;
	int		i;
	trx_uint32_t	data_alloc = 1024, data_offset = 0;

	ptr = *data = (unsigned char *)trx_malloc(NULL, data_alloc);
	trx_serialize_prepare_value(data_offset, results_num);
	(void)trx_serialize_value(ptr, results_num);

	for (i = 0; i < results_num; i++)
	{
		trx_uint32_t	data_len = 0, value_len, error_len;
		trx_am_result_t	*result = results[i];

		trx_serialize_prepare_value(data_len, result->alertid);
		trx_serialize_prepare_value(data_len, result->eventid);
		trx_serialize_prepare_value(data_len, result->mediatypeid);
		trx_serialize_prepare_value(data_len, result->status);
		trx_serialize_prepare_value(data_len, result->retries);
		trx_serialize_prepare_str_len(data_len, result->value, value_len);
		trx_serialize_prepare_str_len(data_len, result->error, error_len);

		while (data_len > data_alloc - data_offset)
		{
			data_alloc *= 2;
			*data = (unsigned char *)trx_realloc(*data, data_alloc);
		}
		ptr = *data + data_offset;
		ptr += trx_serialize_value(ptr, result->alertid);
		ptr += trx_serialize_value(ptr, result->eventid);
		ptr += trx_serialize_value(ptr, result->mediatypeid);
		ptr += trx_serialize_value(ptr, result->status);
		ptr += trx_serialize_value(ptr, result->retries);
		ptr += trx_serialize_str(ptr, result->value, value_len);
		(void)trx_serialize_str(ptr, result->error, error_len);

		data_offset += data_len;
	}

	return data_offset;
}

void	trx_alerter_deserialize_results(const unsigned char *data, trx_am_result_t ***results, int *results_num)
{
	trx_uint32_t	len;
	int		i;

	data += trx_deserialize_value(data, results_num);
	*results = (trx_am_result_t **)trx_malloc(NULL, *results_num * sizeof(trx_am_result_t *));
	for (i = 0; i < *results_num; i++)
	{
		trx_am_result_t	*result;
		result = (trx_am_result_t *)trx_malloc(NULL, sizeof(trx_am_result_t));

		data += trx_deserialize_value(data, &result->alertid);
		data += trx_deserialize_value(data, &result->eventid);
		data += trx_deserialize_value(data, &result->mediatypeid);
		data += trx_deserialize_value(data, &result->status);
		data += trx_deserialize_value(data, &result->retries);
		data += trx_deserialize_str(data, &result->value, len);
		data += trx_deserialize_str(data, &result->error, len);

		(*results)[i] = result;
	}
}

trx_uint32_t	trx_alerter_serialize_ids(unsigned char **data, trx_uint64_t *ids, int ids_num)
{
	unsigned char	*ptr;
	int		i;
	trx_uint32_t	data_alloc = 128, data_offset = 0;

	ptr = *data = (unsigned char *)trx_malloc(NULL, data_alloc);
	trx_serialize_prepare_value(data_offset, ids_num);
	(void)trx_serialize_value(ptr, ids_num);

	for (i = 0; i < ids_num; i++)
	{
		trx_uint32_t	data_len = 0;

		trx_serialize_prepare_value(data_len, ids[i]);

		while (data_len > data_alloc - data_offset)
		{
			data_alloc *= 2;
			*data = (unsigned char *)trx_realloc(*data, data_alloc);
		}
		ptr = *data + data_offset;
		(void)trx_serialize_value(ptr, ids[i]);
		data_offset += data_len;
	}

	return data_offset;
}

void	trx_alerter_deserialize_ids(const unsigned char *data, trx_uint64_t **ids, int *ids_num)
{
	int	i;

	data += trx_deserialize_value(data, ids_num);
	*ids = (trx_uint64_t *)trx_malloc(NULL, *ids_num * sizeof(trx_uint64_t));
	for (i = 0; i < *ids_num; i++)
		data += trx_deserialize_value(data, &(*ids)[i]);
}
