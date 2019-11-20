

#ifndef TREEGIX_JSON_H
#define TREEGIX_JSON_H

#define SKIP_WHITESPACE(src)	\
	while ('\0' != *(src) && NULL != strchr(TRX_WHITESPACE, *(src))) (src)++

/* can only be used on non empty string */
#define SKIP_WHITESPACE_NEXT(src)\
	(src)++; \
	SKIP_WHITESPACE(src)

void	trx_set_json_strerror(const char *fmt, ...) __trx_attr_format_printf(1, 2);
int	trx_json_open_path(const struct trx_json_parse *jp, const char *path, struct trx_json_parse *out);

#endif
