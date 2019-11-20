

#ifndef TREEGIX_ALERTER_PROTOCOL_H
#define TREEGIX_ALERTER_PROTOCOL_H

#include "common.h"

#define TRX_IPC_SERVICE_ALERTER	"alerter"

/* alerter -> manager */
#define TRX_IPC_ALERTER_REGISTER	1000
#define TRX_IPC_ALERTER_RESULT		1001
#define TRX_IPC_ALERTER_ALERT		1002
#define TRX_IPC_ALERTER_MEDIATYPES	1003
#define TRX_IPC_ALERTER_ALERTS		1004
#define TRX_IPC_ALERTER_WATCHDOG	1005
#define TRX_IPC_ALERTER_RESULTS		1006
#define TRX_IPC_ALERTER_DROP_MEDIATYPES	1007

/* manager -> alerter */
#define TRX_IPC_ALERTER_EMAIL		1100
#define TRX_IPC_ALERTER_SMS		1102
#define TRX_IPC_ALERTER_EXEC		1104
#define TRX_IPC_ALERTER_WEBHOOK		1105

#define TRX_WATCHDOG_ALERT_FREQUENCY	(15 * SEC_PER_MIN)

typedef struct
{
	trx_uint64_t	mediaid;
	trx_uint64_t	mediatypeid;
	char		*sendto;
}
trx_am_media_t;

/* media type data */
typedef struct
{
	trx_uint64_t		mediatypeid;

	/* media type data */
	unsigned char		type;
	char			*smtp_server;
	char			*smtp_helo;
	char			*smtp_email;
	char			*exec_path;
	char			*gsm_modem;
	char			*username;
	char			*passwd;
	char			*exec_params;
	char			*timeout;
	char			*script;
	char			*attempt_interval;
	unsigned short		smtp_port;
	unsigned char		smtp_security;
	unsigned char		smtp_verify_peer;
	unsigned char		smtp_verify_host;
	unsigned char		smtp_authentication;

	int			maxsessions;
	int			maxattempts;
	unsigned char		content_type;
	unsigned char		process_tags;
	time_t			last_access;
}
trx_am_db_mediatype_t;

/* alert data */
typedef struct
{
	trx_uint64_t	alertid;
	trx_uint64_t	mediatypeid;
	trx_uint64_t	eventid;
	trx_uint64_t	objectid;

	char		*sendto;
	char		*subject;
	char		*message;
	char		*params;
	int		status;
	int		retries;
	int		source;
	int		object;
}
trx_am_db_alert_t;

/* alert status update data */
typedef struct
{
	trx_uint64_t	alertid;
	trx_uint64_t	eventid;
	trx_uint64_t	mediatypeid;
	int		retries;
	int		status;
	char		*value;
	char		*error;
}
trx_am_result_t;

void	trx_am_db_mediatype_clear(trx_am_db_mediatype_t *mediatype);
void	trx_am_db_alert_free(trx_am_db_alert_t *alert);
void	trx_am_media_clear(trx_am_media_t *media);
void	trx_am_media_free(trx_am_media_t *media);

trx_uint32_t	trx_alerter_serialize_result(unsigned char **data, const char *value, int errcode, const char *error);
void	trx_alerter_deserialize_result(const unsigned char *data, char **value, int *errcode, char **error);

trx_uint32_t	trx_alerter_serialize_email(unsigned char **data, trx_uint64_t alertid, const char *sendto,
		const char *subject, const char *message, const char *smtp_server, unsigned short smtp_port,
		const char *smtp_helo, const char *smtp_email, unsigned char smtp_security,
		unsigned char smtp_verify_peer, unsigned char smtp_verify_host, unsigned char smtp_authentication,
		const char *username, const char *password, unsigned char content_type);

void	trx_alerter_deserialize_email(const unsigned char *data, trx_uint64_t *alertid, char **sendto, char **subject,
		char **message, char **smtp_server, unsigned short *smtp_port, char **smtp_helo, char **smtp_email,
		unsigned char *smtp_security, unsigned char *smtp_verify_peer, unsigned char *smtp_verify_host,
		unsigned char *smtp_authentication, char **username, char **password, unsigned char *content_type);

trx_uint32_t	trx_alerter_serialize_sms(unsigned char **data, trx_uint64_t alertid,  const char *sendto,
		const char *message, const char *gsm_modem);

void	trx_alerter_deserialize_sms(const unsigned char *data, trx_uint64_t *alertid, char **sendto, char **message,
		char **gsm_modem);

trx_uint32_t	trx_alerter_serialize_exec(unsigned char **data, trx_uint64_t alertid, const char *command);

void	trx_alerter_deserialize_exec(const unsigned char *data, trx_uint64_t *alertid, char **command);

trx_uint32_t	trx_alerter_serialize_alert_send(unsigned char **data, trx_uint64_t mediatypeid, unsigned char type,
		const char *smtp_server, const char *smtp_helo, const char *smtp_email, const char *exec_path,
		const char *gsm_modem, const char *username, const char *passwd, unsigned short smtp_port,
		unsigned char smtp_security, unsigned char smtp_verify_peer, unsigned char smtp_verify_host,
		unsigned char smtp_authentication, const char *exec_params, int maxsessions, int maxattempts,
		const char *attempt_interval, unsigned char content_type, const char *script, const char *timeout,
		const char *sendto, const char *subject, const char *message, const char *params);

void	trx_alerter_deserialize_alert_send(const unsigned char *data, trx_uint64_t *mediatypeid,
		unsigned char *type, char **smtp_server, char **smtp_helo, char **smtp_email, char **exec_path,
		char **gsm_modem, char **username, char **passwd, unsigned short *smtp_port,
		unsigned char *smtp_security, unsigned char *smtp_verify_peer, unsigned char *smtp_verify_host,
		unsigned char *smtp_authentication, char **exec_params, int *maxsessions, int *maxattempts,
		char **attempt_interval, unsigned char *content_type, char **script, char **timeout,
		char **sendto, char **subject, char **message, char **params);

trx_uint32_t	trx_alerter_serialize_webhook(unsigned char **data, const char *script_bin, int script_sz,
		int timeout, const char *params);

void	trx_alerter_deserialize_webhook(const unsigned char *data, char **script_bin, int *script_sz, int *timeout,
		char **params);

trx_uint32_t	trx_alerter_serialize_mediatypes(unsigned char **data, trx_am_db_mediatype_t **mediatypes,
		int mediatypes_num);

void	trx_alerter_deserialize_mediatypes(const unsigned char *data, trx_am_db_mediatype_t ***mediatypes,
		int *mediatypes_num);

trx_uint32_t	trx_alerter_serialize_alerts(unsigned char **data, trx_am_db_alert_t **alerts, int alerts_num);

void	trx_alerter_deserialize_alerts(const unsigned char *data, trx_am_db_alert_t ***alerts, int *alerts_num);

trx_uint32_t	trx_alerter_serialize_medias(unsigned char **data, trx_am_media_t **medias, int medias_num);

void	trx_alerter_deserialize_medias(const unsigned char *data, trx_am_media_t ***medias, int *medias_num);

trx_uint32_t	trx_alerter_serialize_results(unsigned char **data, trx_am_result_t **results, int results_num);

void	trx_alerter_deserialize_results(const unsigned char *data, trx_am_result_t ***results, int *results_num);

trx_uint32_t	trx_alerter_serialize_ids(unsigned char **data, trx_uint64_t *ids, int ids_num);

void	trx_alerter_deserialize_ids(const unsigned char *data, trx_uint64_t **ids, int *ids_num);

#endif
