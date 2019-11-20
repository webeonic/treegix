

#ifndef TREEGIX_EMBED_H
#define TREEGIX_EMBED_H

#include "common.h"
#include "duktape.h"

struct trx_es_env
{
	duk_context	*ctx;
	size_t		total_alloc;
	time_t		start_time;

	char		*error;
	int		rt_error_num;
	int		fatal_error;
	int		timeout;

	jmp_buf		loc;
};

#endif /* TREEGIX_EMBED_H */
