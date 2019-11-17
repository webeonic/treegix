

#ifndef TREEGIX_ITEM_PREPROC_H
#define TREEGIX_ITEM_PREPROC_H

#include "dbcache.h"
#include "preproc.h"

int	zbx_item_preproc(unsigned char value_type, zbx_variant_t *value, const zbx_timespec_t *ts,
		const zbx_preproc_op_t *op, zbx_variant_t *history_value, zbx_timespec_t *history_ts, char **error);

int	zbx_item_preproc_handle_error(zbx_variant_t *value, const zbx_preproc_op_t *op, char **error);

int	zbx_item_preproc_convert_value_to_numeric(zbx_variant_t *value_num, const zbx_variant_t *value,
		unsigned char value_type, char **errmsg);

int	zbx_item_preproc_test(unsigned char value_type, zbx_variant_t *value, const zbx_timespec_t *ts,
		zbx_preproc_op_t *steps, int steps_num, zbx_vector_ptr_t *history_in, zbx_vector_ptr_t *history_out,
		zbx_preproc_result_t *results, int *results_num, char **error);

#endif
