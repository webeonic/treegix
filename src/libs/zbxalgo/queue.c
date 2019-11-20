

#include "common.h"
#include "trxalgo.h"

/******************************************************************************
 *                                                                            *
 * Function: trx_queue_ptr_values_num                                         *
 *                                                                            *
 * Purpose: calculates the number of values in queue                          *
 *                                                                            *
 * Parameters: queue - [IN] the queue                                         *
 *                                                                            *
 * Return value: The number of values in queue                                *
 *                                                                            *
 ******************************************************************************/
int	trx_queue_ptr_values_num(trx_queue_ptr_t *queue)
{
	int	values_num;

	values_num = queue->head_pos - queue->tail_pos;
	if (0 > values_num)
		values_num += queue->alloc_num;

	return values_num;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_queue_ptr_reserve                                            *
 *                                                                            *
 * Purpose: reserves space in queue for additional values                     *
 *                                                                            *
 * Parameters: queue - [IN] the queue                                         *
 *             num   - [IN] the number of additional values to reserve        *
 *                                                                            *
 ******************************************************************************/
void	trx_queue_ptr_reserve(trx_queue_ptr_t *queue, int num)
{
	int	values_num, alloc_num, resize_num;

	values_num = trx_queue_ptr_values_num(queue);

	if (values_num + num + 1 <= queue->alloc_num)
		return;

	alloc_num = MAX(queue->alloc_num + num + 1, queue->alloc_num * 1.5);
	queue->values = (void **)trx_realloc(queue->values, alloc_num * sizeof(*queue->values));

	if (queue->tail_pos > queue->head_pos)
	{
		resize_num = alloc_num - queue->alloc_num;
		memmove(queue->values + queue->tail_pos + resize_num, queue->values + queue->tail_pos,
				(queue->alloc_num - queue->tail_pos) * sizeof(*queue->values));
		queue->tail_pos += resize_num;
	}

	queue->alloc_num = alloc_num;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_queue_ptr_compact                                            *
 *                                                                            *
 * Purpose: compacts queue by freeing unused space                            *
 *                                                                            *
 * Parameters: queue - [IN] the queue                                         *
 *                                                                            *
 ******************************************************************************/
void	trx_queue_ptr_compact(trx_queue_ptr_t *queue)
{
	int values_num, alloc_num;

	values_num = trx_queue_ptr_values_num(queue);
	alloc_num = values_num + 1;

	if (alloc_num == queue->alloc_num)
		return;

	if (0 != queue->tail_pos)
	{
		if (queue->tail_pos > queue->head_pos)
		{
			memmove(queue->values + queue->head_pos + 1, queue->values + queue->tail_pos,
					(queue->alloc_num - queue->tail_pos) * sizeof(*queue->values));
			queue->tail_pos = queue->head_pos + 1;
		}
		else
		{
			memmove(queue->values, queue->values + queue->tail_pos, values_num * sizeof(*queue->values));
			queue->tail_pos = 0;
			queue->head_pos = values_num;
		}
	}

	queue->values = (void **)trx_realloc(queue->values, alloc_num * sizeof(*queue->values));
	queue->alloc_num = alloc_num;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_queue_ptr_create                                             *
 *                                                                            *
 * Purpose: creates queue                                                     *
 *                                                                            *
 * Parameters: queue - [IN] the queue                                         *
 *                                                                            *
 ******************************************************************************/
void	trx_queue_ptr_create(trx_queue_ptr_t *queue)
{
	memset(queue, 0, sizeof(*queue));
}

/******************************************************************************
 *                                                                            *
 * Function: trx_queue_ptr_destroy                                            *
 *                                                                            *
 * Purpose: destroys queue                                                    *
 *                                                                            *
 * Parameters: queue - [IN] the queue                                         *
 *                                                                            *
 ******************************************************************************/
void	trx_queue_ptr_destroy(trx_queue_ptr_t *queue)
{
	trx_free(queue->values);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_queue_ptr_push                                               *
 *                                                                            *
 * Purpose: pushes value in the queue                                         *
 *                                                                            *
 * Parameters: queue - [IN] the queue                                         *
 *             elem  - [IN] the value                                         *
 *                                                                            *
 ******************************************************************************/
void	trx_queue_ptr_push(trx_queue_ptr_t *queue, void *value)
{
	trx_queue_ptr_reserve(queue, 1);
	queue->values[queue->head_pos++] = value;

	if (queue->head_pos == queue->alloc_num)
		queue->head_pos = 0;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_queue_ptr_pop                                                *
 *                                                                            *
 * Purpose: pops value in the queue                                           *
 *                                                                            *
 * Parameters: queue - [IN] the queue                                         *
 *                                                                            *
 * Return value: The first queue element.                                     *
 *                                                                            *
 ******************************************************************************/
void	*trx_queue_ptr_pop(trx_queue_ptr_t *queue)
{
	void	*value;

	if (queue->tail_pos != queue->head_pos)
	{
		value = queue->values[queue->tail_pos++];

		if (queue->tail_pos == queue->alloc_num)
			queue->tail_pos = 0;

		if (queue->head_pos == queue->alloc_num)
			queue->head_pos = 0;
	}
	else
		value = NULL;

	return value;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_queue_ptr_remove_value                                       *
 *                                                                            *
 * Purpose: removes specified value from queue                                *
 *                                                                            *
 * Parameters: queue - [IN] the queue                                         *
 *             value - [IN] the value to remove                               *
 *                                                                            *
 ******************************************************************************/
void	trx_queue_ptr_remove_value(trx_queue_ptr_t *queue, const void *value)
{
	int	i, start_pos;

	if (queue->tail_pos == queue->head_pos)
		return;

	if (queue->tail_pos < queue->head_pos)
		start_pos = queue->tail_pos;
	else
		start_pos = 0;

	for (i = start_pos; i < queue->head_pos; i++)
	{
		if (queue->values[i] == value)
		{
			for (; i < queue->head_pos - 1; i++)
				queue->values[i] = queue->values[i + 1];

			queue->head_pos--;
			return;
		}
	}

	if (queue->tail_pos <= queue->head_pos)
		return;

	for (i = queue->alloc_num - 1; i >= queue->tail_pos; i--)
	{
		if (queue->values[i] == value)
		{
			for (; i > queue->tail_pos; i--)
				queue->values[i] = queue->values[i - 1];

			if (++queue->tail_pos == queue->alloc_num)
				queue->tail_pos = 0;

			return;
		}
	}
}
