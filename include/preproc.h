
#ifndef TREEGIX_PREPROC_H
#define TREEGIX_PREPROC_H

#include "common.h"
#include "module.h"
#include "dbcache.h"

/* preprocessing step execution result */
typedef struct
{
	trx_variant_t	value;
	unsigned char	action;
	char		*error;
}
trx_preproc_result_t;

/* the following functions are implemented differently for server and proxy */

void	trx_preprocess_item_value(trx_uint64_t itemid, unsigned char item_value_type, unsigned char item_flags,
		AGENT_RESULT *result, trx_timespec_t *ts, unsigned char state, char *error);
void	trx_preprocessor_flush(void);
trx_uint64_t	trx_preprocessor_get_queue_size(void);

void	trx_preproc_op_free(trx_preproc_op_t *op);
void	trx_preproc_result_free(trx_preproc_result_t *result);

int	trx_preprocessor_test(unsigned char value_type, const char *value, const trx_timespec_t *ts,
		const trx_vector_ptr_t *steps, trx_vector_ptr_t *results, trx_vector_ptr_t *history,
		char **preproc_error, char **error);

#endif /* TREEGIX_PREPROC_H */
