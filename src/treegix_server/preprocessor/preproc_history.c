

#include "common.h"
#include "log.h"

#include "preproc_history.h"

void	zbx_preproc_op_history_free(zbx_preproc_op_history_t *ophistory)
{
	zbx_variant_clear(&ophistory->value);
	zbx_free(ophistory);
}

void	zbx_preproc_history_pop_value(zbx_vector_ptr_t *history, int index, zbx_variant_t *value, zbx_timespec_t *ts)
{
	int				i;
	zbx_preproc_op_history_t	*ophistory;

	for (i = 0; i < history->values_num; i++)
	{
		ophistory = (zbx_preproc_op_history_t *)history->values[i];

		if (ophistory->index == index)
		{
			*value = ophistory->value;
			*ts = ophistory->ts;
			zbx_free(history->values[i]);
			zbx_vector_ptr_remove_noorder(history, i);
			return;
		}
	}

	zbx_variant_set_none(value);
	ts->sec = 0;
	ts->ns = 0;
}

void	zbx_preproc_history_add_value(zbx_vector_ptr_t *history, int index, zbx_variant_t *data,
		const zbx_timespec_t *ts)
{
	zbx_preproc_op_history_t	*ophistory;

	ophistory = zbx_malloc(NULL, sizeof(zbx_preproc_op_history_t));
	ophistory->index = index;
	ophistory->value = *data;
	ophistory->ts = *ts;
	zbx_vector_ptr_append(history, ophistory);

	zbx_variant_set_none(data);
}
