
#ifndef TREEGIX_PREPROC_H
#define TREEGIX_PREPROC_H

#include "common.h"
#include "module.h"
#include "dbcache.h"

/* preprocessing step execution result */
typedef struct
{
	zbx_variant_t	value;
	unsigned char	action;
	char		*error;
}
zbx_preproc_result_t;

/* the following functions are implemented differently for server and proxy */

void	zbx_preprocess_item_value(zbx_uint64_t itemid, unsigned char item_value_type, unsigned char item_flags,
		AGENT_RESULT *result, zbx_timespec_t *ts, unsigned char state, char *error);
void	zbx_preprocessor_flush(void);
zbx_uint64_t	zbx_preprocessor_get_queue_size(void);

void	zbx_preproc_op_free(zbx_preproc_op_t *op);
void	zbx_preproc_result_free(zbx_preproc_result_t *result);

int	zbx_preprocessor_test(unsigned char value_type, const char *value, const zbx_timespec_t *ts,
		const zbx_vector_ptr_t *steps, zbx_vector_ptr_t *results, zbx_vector_ptr_t *history,
		char **preproc_error, char **error);

#endif /* TREEGIX_PREPROC_H */
