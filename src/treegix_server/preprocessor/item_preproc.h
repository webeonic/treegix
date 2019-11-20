

#ifndef TREEGIX_ITEM_PREPROC_H
#define TREEGIX_ITEM_PREPROC_H

#include "dbcache.h"
#include "preproc.h"

int	trx_item_preproc(unsigned char value_type, trx_variant_t *value, const trx_timespec_t *ts,
		const trx_preproc_op_t *op, trx_variant_t *history_value, trx_timespec_t *history_ts, char **error);

int	trx_item_preproc_handle_error(trx_variant_t *value, const trx_preproc_op_t *op, char **error);

int	trx_item_preproc_convert_value_to_numeric(trx_variant_t *value_num, const trx_variant_t *value,
		unsigned char value_type, char **errmsg);

int	trx_item_preproc_test(unsigned char value_type, trx_variant_t *value, const trx_timespec_t *ts,
		trx_preproc_op_t *steps, int steps_num, trx_vector_ptr_t *history_in, trx_vector_ptr_t *history_out,
		trx_preproc_result_t *results, int *results_num, char **error);

#endif
