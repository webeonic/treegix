

#ifndef TREEGIX_SCRIPTS_H
#define TREEGIX_SCRIPTS_H

#include "common.h"
#include "dbcache.h"

void	trx_script_init(trx_script_t *script);
void	trx_script_clean(trx_script_t *script);
int	trx_script_execute(const trx_script_t *script, const DC_HOST *host, char **result, char *error, size_t max_error_len);
int	trx_script_prepare(trx_script_t *script, const DC_HOST *host, const trx_user_t *user, char *error,
		size_t max_error_len);
trx_uint64_t	trx_script_create_task(const trx_script_t *script, const DC_HOST *host, trx_uint64_t alertid, int now);
#endif
