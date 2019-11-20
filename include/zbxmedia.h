
#ifndef TREEGIX_TRXMEDIA_H
#define TREEGIX_TRXMEDIA_H

#include "sysinc.h" /* using "config.h" would be better, but it causes warnings when compiled with Net-SNMP */

#define TRX_MEDIA_CONTENT_TYPE_TEXT	0
#define TRX_MEDIA_CONTENT_TYPE_HTML	1

extern char	*CONFIG_SOURCE_IP;

typedef struct
{
	char		*addr;
	char		*disp_name;
}
trx_mailaddr_t;

int	send_email(const char *smtp_server, unsigned short smtp_port, const char *smtp_helo,
		const char *smtp_email, const char *mailto, const char *mailsubject, const char *mailbody,
		unsigned char smtp_security, unsigned char smtp_verify_peer, unsigned char smtp_verify_host,
		unsigned char smtp_authentication, const char *username, const char *password,
		unsigned char content_type, int timeout, char *error, size_t max_error_len);
int	send_sms(const char *device, const char *number, const char *message, char *error, int max_error_len);

#endif
