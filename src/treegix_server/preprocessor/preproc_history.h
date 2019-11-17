

#ifndef TREEGIX_PREPROC_HISTORY_H
#define TREEGIX_PREPROC_HISTORY_H

#include "common.h"
#include "dbcache.h"

typedef struct
{
	int		index;
	zbx_variant_t	value;
	zbx_timespec_t	ts;
}
zbx_preproc_op_history_t;

typedef struct
{
	zbx_uint64_t		itemid;
	zbx_vector_ptr_t	history;
}
zbx_preproc_history_t;

void	zbx_preproc_op_history_free(zbx_preproc_op_history_t *ophistory);
void	zbx_preproc_history_pop_value(zbx_vector_ptr_t *history, int index, zbx_variant_t *value, zbx_timespec_t *ts);
void	zbx_preproc_history_add_value(zbx_vector_ptr_t *history, int index, zbx_variant_t *data,
		const zbx_timespec_t *ts);

#endif
