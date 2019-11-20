

#ifndef TREEGIX_LLD_H
#define TREEGIX_LLD_H

#include "common.h"

void	trx_lld_process_value(trx_uint64_t itemid, const char *value, const trx_timespec_t *ts, unsigned char meta,
		trx_uint64_t lastlogsize, int mtime, const char *error);

void	trx_lld_process_agent_result(trx_uint64_t itemid, AGENT_RESULT *result, trx_timespec_t *ts, char *error);

int	trx_lld_get_queue_size(trx_uint64_t *size, char **error);

#endif	/* TREEGIX_LLD_H */
