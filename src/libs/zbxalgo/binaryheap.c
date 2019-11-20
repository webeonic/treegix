

#include "common.h"
#include "log.h"

#include "trxalgo.h"

static void	swap(trx_binary_heap_t *heap, int index_1, int index_2);

static void	__binary_heap_ensure_free_space(trx_binary_heap_t *heap);

static int	__binary_heap_bubble_up(trx_binary_heap_t *heap, int index);
static int	__binary_heap_bubble_down(trx_binary_heap_t *heap, int index);

#define	ARRAY_GROWTH_FACTOR	3/2

#define	HAS_DIRECT_OPTION(heap)	(0 != (heap->options & TRX_BINARY_HEAP_OPTION_DIRECT))

/* helper functions */

static void	swap(trx_binary_heap_t *heap, int index_1, int index_2)
{
	trx_binary_heap_elem_t	tmp;

	tmp = heap->elems[index_1];
	heap->elems[index_1] = heap->elems[index_2];
	heap->elems[index_2] = tmp;

	if (HAS_DIRECT_OPTION(heap))
	{
		trx_hashmap_set(heap->key_index, heap->elems[index_1].key, index_1);
		trx_hashmap_set(heap->key_index, heap->elems[index_2].key, index_2);
	}
}

/* private binary heap functions */

static void	__binary_heap_ensure_free_space(trx_binary_heap_t *heap)
{
	int	tmp_elems_alloc = heap->elems_alloc;

	/* In order to prevent memory corruption heap->elems_alloc is set only after successful allocation. */
	/* Otherwise, in case of shared memory, other processes might read or write past the actually       */
	/* allocated memory.                                                                                */

	if (NULL == heap->elems)
	{
		heap->elems_num = 0;
		tmp_elems_alloc = 32;
	}
	else if (heap->elems_num == heap->elems_alloc)
		tmp_elems_alloc = MAX(heap->elems_alloc + 1, heap->elems_alloc * ARRAY_GROWTH_FACTOR);

	if (heap->elems_alloc != tmp_elems_alloc)
	{
		heap->elems = (trx_binary_heap_elem_t *)heap->mem_realloc_func(heap->elems, tmp_elems_alloc * sizeof(trx_binary_heap_elem_t));

		if (NULL == heap->elems)
		{
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
		}

		heap->elems_alloc = tmp_elems_alloc;
	}
}

static int	__binary_heap_bubble_up(trx_binary_heap_t *heap, int index)
{
	while (0 != index)
	{
		if (heap->compare_func(&heap->elems[(index - 1) / 2], &heap->elems[index]) <= 0)
			break;

		swap(heap, (index - 1) / 2, index);
		index = (index - 1) / 2;
	}

	return index;
}

static int	__binary_heap_bubble_down(trx_binary_heap_t *heap, int index)
{
	while (1)
	{
		int left = 2 * index + 1;
		int right = 2 * index + 2;

		if (left >= heap->elems_num)
			break;

		if (right >= heap->elems_num)
		{
			if (heap->compare_func(&heap->elems[index], &heap->elems[left]) > 0)
			{
				swap(heap, index, left);
				index = left;
			}

			break;
		}

		if (heap->compare_func(&heap->elems[left], &heap->elems[right]) <= 0)
		{
			if (heap->compare_func(&heap->elems[index], &heap->elems[left]) > 0)
			{
				swap(heap, index, left);
				index = left;
			}
			else
				break;
		}
		else
		{
			if (heap->compare_func(&heap->elems[index], &heap->elems[right]) > 0)
			{
				swap(heap, index, right);
				index = right;
			}
			else
				break;
		}
	}

	return index;
}

/* public binary heap interface */

void	trx_binary_heap_create(trx_binary_heap_t *heap, trx_compare_func_t compare_func, int options)
{
	trx_binary_heap_create_ext(heap, compare_func, options,
					TRX_DEFAULT_MEM_MALLOC_FUNC,
					TRX_DEFAULT_MEM_REALLOC_FUNC,
					TRX_DEFAULT_MEM_FREE_FUNC);
}

void	trx_binary_heap_create_ext(trx_binary_heap_t *heap, trx_compare_func_t compare_func, int options,
					trx_mem_malloc_func_t mem_malloc_func,
					trx_mem_realloc_func_t mem_realloc_func,
					trx_mem_free_func_t mem_free_func)
{
	heap->elems = NULL;
	heap->elems_num = 0;
	heap->elems_alloc = 0;
	heap->compare_func = compare_func;
	heap->options = options;

	if (HAS_DIRECT_OPTION(heap))
	{
		heap->key_index = (trx_hashmap_t *)mem_malloc_func(NULL, sizeof(trx_hashmap_t));
		trx_hashmap_create_ext(heap->key_index, 512,
					TRX_DEFAULT_UINT64_HASH_FUNC,
					TRX_DEFAULT_UINT64_COMPARE_FUNC,
					mem_malloc_func,
					mem_realloc_func,
					mem_free_func);
	}
	else
		heap->key_index = NULL;

	heap->mem_malloc_func = mem_malloc_func;
	heap->mem_realloc_func = mem_realloc_func;
	heap->mem_free_func = mem_free_func;
}

void	trx_binary_heap_destroy(trx_binary_heap_t *heap)
{
	if (NULL != heap->elems)
	{
		heap->mem_free_func(heap->elems);
		heap->elems = NULL;
		heap->elems_num = 0;
		heap->elems_alloc = 0;
	}

	heap->compare_func = NULL;

	if (HAS_DIRECT_OPTION(heap))
	{
		trx_hashmap_destroy(heap->key_index);
		heap->mem_free_func(heap->key_index);
		heap->key_index = NULL;
		heap->options = 0;
	}

	heap->mem_malloc_func = NULL;
	heap->mem_realloc_func = NULL;
	heap->mem_free_func = NULL;
}

int	trx_binary_heap_empty(trx_binary_heap_t *heap)
{
	return (0 == heap->elems_num ? SUCCEED : FAIL);
}

trx_binary_heap_elem_t	*trx_binary_heap_find_min(trx_binary_heap_t *heap)
{
	if (0 == heap->elems_num)
	{
		treegix_log(LOG_LEVEL_CRIT, "asking for a minimum in an empty heap");
		exit(EXIT_FAILURE);
	}

	return &heap->elems[0];
}

void	trx_binary_heap_insert(trx_binary_heap_t *heap, trx_binary_heap_elem_t *elem)
{
	int	index;

	if (HAS_DIRECT_OPTION(heap) && FAIL != trx_hashmap_get(heap->key_index, elem->key))
	{
		treegix_log(LOG_LEVEL_CRIT, "inserting a duplicate key into a heap with direct option");
		exit(EXIT_FAILURE);
	}

	__binary_heap_ensure_free_space(heap);

	index = heap->elems_num++;
	heap->elems[index] = *elem;

	index = __binary_heap_bubble_up(heap, index);

	if (HAS_DIRECT_OPTION(heap) && index == heap->elems_num - 1)
		trx_hashmap_set(heap->key_index, elem->key, index);
}

void	trx_binary_heap_update_direct(trx_binary_heap_t *heap, trx_binary_heap_elem_t *elem)
{
	int	index;

	if (!HAS_DIRECT_OPTION(heap))
	{
		treegix_log(LOG_LEVEL_CRIT, "direct update operation is not supported for this heap");
		exit(EXIT_FAILURE);
	}

	if (FAIL != (index = trx_hashmap_get(heap->key_index, elem->key)))
	{
		heap->elems[index] = *elem;

		if (index == __binary_heap_bubble_up(heap, index))
			__binary_heap_bubble_down(heap, index);
	}
	else
	{
		treegix_log(LOG_LEVEL_CRIT, "element with key " TRX_FS_UI64 " not found in heap for update", elem->key);
		exit(EXIT_FAILURE);
	}
}

void	trx_binary_heap_remove_min(trx_binary_heap_t *heap)
{
	int	index;

	if (0 == heap->elems_num)
	{
		treegix_log(LOG_LEVEL_CRIT, "removing a minimum from an empty heap");
		exit(EXIT_FAILURE);
	}

	if (HAS_DIRECT_OPTION(heap))
		trx_hashmap_remove(heap->key_index, heap->elems[0].key);

	if (0 != (--heap->elems_num))
	{
		heap->elems[0] = heap->elems[heap->elems_num];
		index = __binary_heap_bubble_down(heap, 0);

		if (HAS_DIRECT_OPTION(heap) && index == 0)
			trx_hashmap_set(heap->key_index, heap->elems[index].key, index);
	}
}

void	trx_binary_heap_remove_direct(trx_binary_heap_t *heap, trx_uint64_t key)
{
	int	index;

	if (!HAS_DIRECT_OPTION(heap))
	{
		treegix_log(LOG_LEVEL_CRIT, "direct remove operation is not supported for this heap");
		exit(EXIT_FAILURE);
	}

	if (FAIL != (index = trx_hashmap_get(heap->key_index, key)))
	{
		trx_hashmap_remove(heap->key_index, key);

		if (index != (--heap->elems_num))
		{
			heap->elems[index] = heap->elems[heap->elems_num];

			if (index == __binary_heap_bubble_up(heap, index))
				if (index == __binary_heap_bubble_down(heap, index))
					trx_hashmap_set(heap->key_index, heap->elems[index].key, index);
		}
	}
	else
	{
		treegix_log(LOG_LEVEL_CRIT, "element with key " TRX_FS_UI64 " not found in heap for remove", key);
		exit(EXIT_FAILURE);
	}
}

void	trx_binary_heap_clear(trx_binary_heap_t *heap)
{
	heap->elems_num = 0;

	if (HAS_DIRECT_OPTION(heap))
		trx_hashmap_clear(heap->key_index);
}
