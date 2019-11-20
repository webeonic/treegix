

#ifndef TREEGIX_PREPROC_HISTORY_H
#define TREEGIX_PREPROC_HISTORY_H

#include "common.h"
#include "dbcache.h"

typedef struct
{
	int		index;
	trx_variant_t	value;
	trx_timespec_t	ts;
}
trx_preproc_op_history_t;

typedef struct
{
	trx_uint64_t		itemid;
	trx_vector_ptr_t	history;
}
trx_preproc_history_t;

void	trx_preproc_op_history_free(trx_preproc_op_history_t *ophistory);
void	trx_preproc_history_pop_value(trx_vector_ptr_t *history, int index, trx_variant_t *value, trx_timespec_t *ts);
void	trx_preproc_history_add_value(trx_vector_ptr_t *history, int index, trx_variant_t *data,
		const trx_timespec_t *ts);

#endif
