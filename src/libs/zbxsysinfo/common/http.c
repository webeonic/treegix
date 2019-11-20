

#include "common.h"
#include "sysinfo.h"
#include "trxregexp.h"
#include "trxhttp.h"

#include "comms.h"
#include "cfg.h"

#include "http.h"

#define HTTP_SCHEME_STR		"http://"

#ifndef HAVE_LIBCURL

#define TRX_MAX_WEBPAGE_SIZE	(1 * 1024 * 1024)

#else

#define HTTPS_SCHEME_STR	"https://"

typedef struct
{
	char	*data;
	size_t	allocated;
	size_t	offset;
}
trx_http_response_t;

#endif

static int	detect_url(const char *host)
{
	char	*p;
	int	ret = FAIL;

	if (NULL != strpbrk(host, "/@#?[]"))
		return SUCCEED;

	if (NULL != (p = strchr(host, ':')) && NULL == strchr(++p, ':'))
		ret = SUCCEED;

	return ret;
}

static int	process_url(const char *host, const char *port, const char *path, char **url, char **error)
{
	char	*p, *delim;
	int	scheme_found = 0;

	/* port and path parameters must be empty */
	if ((NULL != port && '\0' != *port) || (NULL != path && '\0' != *path))
	{
		*error = trx_strdup(*error,
				"Parameters \"path\" and \"port\" must be empty if URL is specified in \"host\".");
		return FAIL;
	}

	/* allow HTTP(S) scheme only */
#ifdef HAVE_LIBCURL
	if (0 == trx_strncasecmp(host, HTTP_SCHEME_STR, TRX_CONST_STRLEN(HTTP_SCHEME_STR)) ||
			0 == trx_strncasecmp(host, HTTPS_SCHEME_STR, TRX_CONST_STRLEN(HTTPS_SCHEME_STR)))
#else
	if (0 == trx_strncasecmp(host, HTTP_SCHEME_STR, TRX_CONST_STRLEN(HTTP_SCHEME_STR)))
#endif
	{
		scheme_found = 1;
	}
	else if (NULL != (p = strstr(host, "://")) && (NULL == (delim = strpbrk(host, "/?#")) || delim > p))
	{
		*error = trx_dsprintf(*error, "Unsupported scheme: %.*s.", (int)(p - host), host);
		return FAIL;
	}

	if (NULL != (p = strchr(host, '#')))
		*url = trx_dsprintf(*url, "%s%.*s", (0 == scheme_found ? HTTP_SCHEME_STR : ""), (int)(p - host), host);
	else
		*url = trx_dsprintf(*url, "%s%s", (0 == scheme_found ? HTTP_SCHEME_STR : ""), host);

	return SUCCEED;
}

static int	check_common_params(const char *host, const char *path, char **error)
{
	const char	*wrong_chr, URI_PROHIBIT_CHARS[] = {0x1,0x2,0x3,0x4,0x5,0x6,0x7,0x8,0x9,0xA,0xB,0xC,0xD,0xE,\
			0xF,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x7F,0};

	if (NULL == host || '\0' == *host)
	{
		*error = trx_strdup(*error, "Invalid first parameter.");
		return FAIL;
	}

	if (NULL != (wrong_chr = strpbrk(host, URI_PROHIBIT_CHARS)))
	{
		*error = trx_dsprintf(NULL, "Incorrect hostname expression. Check hostname part after: %.*s.",
				(int)(wrong_chr - host), host);
		return FAIL;
	}

	if (NULL != path && NULL != (wrong_chr = strpbrk(path, URI_PROHIBIT_CHARS)))
	{
		*error = trx_dsprintf(NULL, "Incorrect path expression. Check path part after: %.*s.",
				(int)(wrong_chr - path), path);
		return FAIL;
	}

	return SUCCEED;
}

#ifdef HAVE_LIBCURL
static size_t	curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	size_t			r_size = size * nmemb;
	trx_http_response_t	*response;

	response = (trx_http_response_t*)userdata;
	trx_str_memcpy_alloc(&response->data, &response->allocated, &response->offset, (const char *)ptr, r_size);

	return r_size;
}

static size_t	curl_ignore_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	TRX_UNUSED(ptr);
	TRX_UNUSED(userdata);

	return size * nmemb;
}

static int	curl_page_get(char *url, char **buffer, char **error)
{
	CURLcode		err;
	trx_http_response_t	page = {0};
	CURL			*easyhandle;
	int			ret = SYSINFO_RET_FAIL;

	if (NULL == (easyhandle = curl_easy_init()))
	{
		*error = trx_strdup(*error, "Cannot initialize cURL library.");
		return SYSINFO_RET_FAIL;
	}

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_USERAGENT, "Treegix " TREEGIX_VERSION)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_SSL_VERIFYPEER, 0L)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_SSL_VERIFYHOST, 0L)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_FOLLOWLOCATION, 0L)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_URL, url)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_WRITEFUNCTION,
			NULL != buffer ? curl_write_cb : curl_ignore_cb)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_WRITEDATA, &page)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_HEADER, 1L)) ||
			(NULL != CONFIG_SOURCE_IP &&
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_INTERFACE, CONFIG_SOURCE_IP))))
	{
		*error = trx_dsprintf(*error, "Cannot set cURL option: %s.", curl_easy_strerror(err));
		goto out;
	}

	if (CURLE_OK == (err = curl_easy_perform(easyhandle)))
	{
		if (NULL != buffer)
			*buffer = page.data;

		ret = SYSINFO_RET_OK;
	}
	else
	{
		trx_free(page.data);
		*error = trx_dsprintf(*error, "Cannot perform cURL request: %s.", curl_easy_strerror(err));
	}

out:
	curl_easy_cleanup(easyhandle);

	return ret;
}

static int	get_http_page(const char *host, const char *path, const char *port, char **buffer, char **error)
{
	char	*url = NULL;
	int	ret;

	if (SUCCEED != check_common_params(host, path, error))
		return SYSINFO_RET_FAIL;

	if (SUCCEED == detect_url(host))
	{
		/* URL detected */
		if (SUCCEED != process_url(host, port, path, &url, error))
			return SYSINFO_RET_FAIL;
	}
	else
	{
		/* URL is not detected - compose URL using host, port and path */

		unsigned short	port_n = TRX_DEFAULT_HTTP_PORT;

		if (NULL != port && '\0' != *port)
		{
			if (SUCCEED != is_ushort(port, &port_n))
			{
				*error = trx_strdup(*error, "Invalid third parameter.");
				return SYSINFO_RET_FAIL;
			}
		}

		if (NULL != strchr(host, ':'))
			url = trx_dsprintf(url, HTTP_SCHEME_STR "[%s]:%u/", host, port_n);
		else
			url = trx_dsprintf(url, HTTP_SCHEME_STR "%s:%u/", host, port_n);

		if (NULL != path)
			url = trx_strdcat(url, path + ('/' == *path ? 1 : 0));
	}

	if (SUCCEED != trx_http_punycode_encode_url(&url))
	{
		*error = trx_strdup(*error, "Cannot encode domain name into punycode.");
		ret = SYSINFO_RET_FAIL;
		goto out;
	}

	ret = curl_page_get(url, buffer, error);
out:
	trx_free(url);

	return ret;
}
#else
static char	*find_port_sep(char *host, size_t len)
{
	int	in_ipv6 = 0;

	for (; 0 < len--; host++)
	{
		if (0 == in_ipv6)
		{
			if (':' == *host)
				return host;
			else if ('[' == *host)
				in_ipv6 = 1;
		}
		else if (']' == *host)
			in_ipv6 = 0;
	}

	return NULL;
}

static int	get_http_page(const char *host, const char *path, const char *port, char **buffer, char **error)
{
	char		*url = NULL, *hostname = NULL, *path_loc = NULL;
	int		ret = SYSINFO_RET_OK, ipv6_host_found = 0;
	unsigned short	port_num;
	trx_socket_t	s;

	if (SUCCEED != check_common_params(host, path, error))
		return SYSINFO_RET_FAIL;

	if (SUCCEED == detect_url(host))
	{
		/* URL detected */

		char	*p, *p_host, *au_end;
		size_t	authority_len;

		if (SUCCEED != process_url(host, port, path, &url, error))
			return SYSINFO_RET_FAIL;

		p_host = url + TRX_CONST_STRLEN(HTTP_SCHEME_STR);

		if (0 == (authority_len = strcspn(p_host, "/?")))
		{
			*error = trx_dsprintf(*error, "Invalid or missing host in URL.");
			ret = SYSINFO_RET_FAIL;
			goto out;
		}

		if (NULL != memchr(p_host, '@', authority_len))
		{
			*error = trx_strdup(*error, "Unsupported URL format.");
			ret = SYSINFO_RET_FAIL;
			goto out;
		}

		au_end = &p_host[authority_len - 1];

		if (NULL != (p = find_port_sep(p_host, authority_len)))
		{
			char	*port_str;
			int	port_len = (int)(au_end - p);

			if (0 < port_len)
			{
				port_str = trx_dsprintf(NULL, "%.*s", port_len, p + 1);

				if (SUCCEED != is_ushort(port_str, &port_num))
					ret = SYSINFO_RET_FAIL;
				else
					hostname = trx_dsprintf(hostname, "%.*s", (int)(p - p_host), p_host);

				trx_free(port_str);
			}
			else
				ret = SYSINFO_RET_FAIL;
		}
		else
		{
			port_num = TRX_DEFAULT_HTTP_PORT;
			hostname = trx_dsprintf(hostname, "%.*s", (int)(au_end - p_host + 1), p_host);
		}

		if (SYSINFO_RET_OK != ret)
		{
			*error = trx_dsprintf(*error, "URL using bad/illegal format.");
			goto out;
		}

		if ('[' == *hostname)
		{
			trx_ltrim(hostname, "[");
			trx_rtrim(hostname, "]");
			ipv6_host_found = 1;
		}

		if ('\0' == *hostname)
		{
			*error = trx_dsprintf(*error, "Invalid or missing host in URL.");
			ret = SYSINFO_RET_FAIL;
			goto out;
		}

		path_loc = trx_strdup(path_loc, '\0' != p_host[authority_len] ? &p_host[authority_len] : "/");
	}
	else
	{
		/* URL is not detected */

		if (NULL == port || '\0' == *port)
		{
			port_num = TRX_DEFAULT_HTTP_PORT;
		}
		else if (FAIL == is_ushort(port, &port_num))
		{
			*error = trx_strdup(*error, "Invalid third parameter.");
			ret = SYSINFO_RET_FAIL;
			goto out;
		}

		path_loc = trx_strdup(path_loc, (NULL != path ? path : "/"));
		hostname = trx_strdup(hostname, host);

		if (NULL != strchr(hostname, ':'))
			ipv6_host_found = 1;
	}

	if (SUCCEED != trx_http_punycode_encode_url(&hostname))
	{
		*error = trx_strdup(*error, "Cannot encode domain name into punycode.");
		ret = SYSINFO_RET_FAIL;
		goto out;
	}

	if (SUCCEED == (ret = trx_tcp_connect(&s, CONFIG_SOURCE_IP, hostname, port_num, CONFIG_TIMEOUT,
			TRX_TCP_SEC_UNENCRYPTED, NULL, NULL)))
	{
		char	*request = NULL;

		request = trx_dsprintf(request,
				"GET %s%s HTTP/1.1\r\n"
				"Host: %s%s%s\r\n"
				"Connection: close\r\n"
				"\r\n",
				('/' != *path_loc ? "/" : ""), path_loc, (1 == ipv6_host_found ? "[" : ""), hostname,
				(1 == ipv6_host_found ? "]" : ""));

		if (SUCCEED == (ret = trx_tcp_send_raw(&s, request)))
		{
			if (SUCCEED == (ret = trx_tcp_recv_raw(&s)))
			{
				if (NULL != buffer)
				{
					*buffer = (char*)trx_malloc(*buffer, TRX_MAX_WEBPAGE_SIZE);
					trx_strlcpy(*buffer, s.buffer, TRX_MAX_WEBPAGE_SIZE);
				}
			}
		}

		trx_free(request);
		trx_tcp_close(&s);
	}

	if (SUCCEED != ret)
	{
		*error = trx_dsprintf(NULL, "HTTP get error: %s", trx_socket_strerror());
		ret = SYSINFO_RET_FAIL;
	}
	else
		ret = SYSINFO_RET_OK;

out:
	trx_free(url);
	trx_free(path_loc);
	trx_free(hostname);

	return ret;
}
#endif

int	WEB_PAGE_GET(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*hostname, *path_str, *port_str, *buffer = NULL, *error = NULL;
	int	ret;

	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	hostname = get_rparam(request, 0);
	path_str = get_rparam(request, 1);
	port_str = get_rparam(request, 2);

	if (SYSINFO_RET_OK == (ret = get_http_page(hostname, path_str, port_str, &buffer, &error)))
	{
		trx_rtrim(buffer, "\r\n");
		SET_TEXT_RESULT(result, buffer);
	}
	else
		SET_MSG_RESULT(result, error);

	return ret;
}

int	WEB_PAGE_PERF(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*hostname, *path_str, *port_str, *error = NULL;
	double	start_time;
	int	ret;

	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	hostname = get_rparam(request, 0);
	path_str = get_rparam(request, 1);
	port_str = get_rparam(request, 2);

	start_time = trx_time();

	if (SYSINFO_RET_OK == (ret = get_http_page(hostname, path_str, port_str, NULL, &error)))
		SET_DBL_RESULT(result, trx_time() - start_time);
	else
		SET_MSG_RESULT(result, error);

	return ret;
}

int	WEB_PAGE_REGEXP(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		*hostname, *path_str, *port_str, *buffer = NULL, *error = NULL,
			*ptr = NULL, *str, *newline, *regexp, *length_str;
	const char	*output;
	int		length, ret;

	if (6 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	if (4 > request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
		return SYSINFO_RET_FAIL;
	}

	hostname = get_rparam(request, 0);
	path_str = get_rparam(request, 1);
	port_str = get_rparam(request, 2);
	regexp = get_rparam(request, 3);
	length_str = get_rparam(request, 4);
	output = get_rparam(request, 5);

	if (NULL == length_str || '\0' == *length_str)
		length = MAX_BUFFER_LEN - 1;
	else if (FAIL == is_uint31_1(length_str, &length))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid fifth parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* by default return the matched part of web page */
	if (NULL == output || '\0' == *output)
		output = "\\0";

	if (SYSINFO_RET_OK == (ret = get_http_page(hostname, path_str, port_str, &buffer, &error)))
	{
		for (str = buffer; ;)
		{
			if (NULL != (newline = strchr(str, '\n')))
			{
				if (str != newline && '\r' == newline[-1])
					newline[-1] = '\0';
				else
					*newline = '\0';
			}

			if (SUCCEED == trx_regexp_sub(str, regexp, output, &ptr) && NULL != ptr)
				break;

			if (NULL != newline)
				str = newline + 1;
			else
				break;
		}

		if (NULL != ptr)
			SET_STR_RESULT(result, ptr);
		else
			SET_STR_RESULT(result, trx_strdup(NULL, ""));

		trx_free(buffer);
	}
	else
		SET_MSG_RESULT(result, error);

	return ret;
}
