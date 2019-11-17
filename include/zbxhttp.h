
#ifndef TREEGIX_ZBXHTTP_H
#define TREEGIX_ZBXHTTP_H

#include "common.h"

int	zbx_http_punycode_encode_url(char **url);
void	zbx_http_url_encode(const char *source, char **result);
int	zbx_http_url_decode(const char *source, char **result);

#ifdef HAVE_LIBCURL
int	zbx_http_prepare_ssl(CURL *easyhandle, const char *ssl_cert_file, const char *ssl_key_file,
		const char *ssl_key_password, unsigned char verify_peer, unsigned char verify_host, char **error);
int	zbx_http_prepare_auth(CURL *easyhandle, unsigned char authtype, const char *username, const char *password,
		char **error);
char	*zbx_http_get_header(char **headers);
#endif

#endif
