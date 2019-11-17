

#ifndef TREEGIX_LLD_H
#define TREEGIX_LLD_H

#include "common.h"

void	zbx_lld_process_value(zbx_uint64_t itemid, const char *value, const zbx_timespec_t *ts, unsigned char meta,
		zbx_uint64_t lastlogsize, int mtime, const char *error);

void	zbx_lld_process_agent_result(zbx_uint64_t itemid, AGENT_RESULT *result, zbx_timespec_t *ts, char *error);

int	zbx_lld_get_queue_size(zbx_uint64_t *size, char **error);

#endif	/* TREEGIX_LLD_H */
