

#include "common.h"
#include "log.h"

#include "preproc_history.h"

void	trx_preproc_op_history_free(trx_preproc_op_history_t *ophistory)
{
	trx_variant_clear(&ophistory->value);
	trx_free(ophistory);
}

void	trx_preproc_history_pop_value(trx_vector_ptr_t *history, int index, trx_variant_t *value, trx_timespec_t *ts)
{
	int				i;
	trx_preproc_op_history_t	*ophistory;

	for (i = 0; i < history->values_num; i++)
	{
		ophistory = (trx_preproc_op_history_t *)history->values[i];

		if (ophistory->index == index)
		{
			*value = ophistory->value;
			*ts = ophistory->ts;
			trx_free(history->values[i]);
			trx_vector_ptr_remove_noorder(history, i);
			return;
		}
	}

	trx_variant_set_none(value);
	ts->sec = 0;
	ts->ns = 0;
}

void	trx_preproc_history_add_value(trx_vector_ptr_t *history, int index, trx_variant_t *data,
		const trx_timespec_t *ts)
{
	trx_preproc_op_history_t	*ophistory;

	ophistory = trx_malloc(NULL, sizeof(trx_preproc_op_history_t));
	ophistory->index = index;
	ophistory->value = *data;
	ophistory->ts = *ts;
	trx_vector_ptr_append(history, ophistory);

	trx_variant_set_none(data);
}
