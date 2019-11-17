

#ifndef TREEGIX_SCRIPTS_H
#define TREEGIX_SCRIPTS_H

#include "common.h"
#include "dbcache.h"

void	zbx_script_init(zbx_script_t *script);
void	zbx_script_clean(zbx_script_t *script);
int	zbx_script_execute(const zbx_script_t *script, const DC_HOST *host, char **result, char *error, size_t max_error_len);
int	zbx_script_prepare(zbx_script_t *script, const DC_HOST *host, const zbx_user_t *user, char *error,
		size_t max_error_len);
zbx_uint64_t	zbx_script_create_task(const zbx_script_t *script, const DC_HOST *host, zbx_uint64_t alertid, int now);
#endif
