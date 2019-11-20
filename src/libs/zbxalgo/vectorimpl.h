

#ifndef TREEGIX_VECTORIMPL_H
#define TREEGIX_VECTORIMPL_H

#define	TRX_VECTOR_ARRAY_GROWTH_FACTOR	3/2

#define	TRX_VECTOR_IMPL(__id, __type)										\
														\
static void	__vector_ ## __id ## _ensure_free_space(trx_vector_ ## __id ## _t *vector)			\
{														\
	if (NULL == vector->values)										\
	{													\
		vector->values_num = 0;										\
		vector->values_alloc = 32;									\
		vector->values = (__type *)vector->mem_malloc_func(NULL, vector->values_alloc * sizeof(__type));		\
	}													\
	else if (vector->values_num == vector->values_alloc)							\
	{													\
		vector->values_alloc = MAX(vector->values_alloc + 1, vector->values_alloc * TRX_VECTOR_ARRAY_GROWTH_FACTOR); \
		vector->values = (__type *)vector->mem_realloc_func(vector->values, vector->values_alloc * sizeof(__type)); \
	}													\
}														\
														\
void	trx_vector_ ## __id ## _create(trx_vector_ ## __id ## _t *vector)					\
{														\
	trx_vector_ ## __id ## _create_ext(vector,								\
						TRX_DEFAULT_MEM_MALLOC_FUNC,					\
						TRX_DEFAULT_MEM_REALLOC_FUNC,					\
						TRX_DEFAULT_MEM_FREE_FUNC);					\
}														\
														\
void	trx_vector_ ## __id ## _create_ext(trx_vector_ ## __id ## _t *vector,					\
						trx_mem_malloc_func_t mem_malloc_func,				\
						trx_mem_realloc_func_t mem_realloc_func,			\
						trx_mem_free_func_t mem_free_func)				\
{														\
	vector->values = NULL;											\
	vector->values_num = 0;											\
	vector->values_alloc = 0;										\
														\
	vector->mem_malloc_func = mem_malloc_func;								\
	vector->mem_realloc_func = mem_realloc_func;								\
	vector->mem_free_func = mem_free_func;									\
}														\
														\
void	trx_vector_ ## __id ## _destroy(trx_vector_ ## __id ## _t *vector)					\
{														\
	if (NULL != vector->values)										\
	{													\
		vector->mem_free_func(vector->values);								\
		vector->values = NULL;										\
		vector->values_num = 0;										\
		vector->values_alloc = 0;									\
	}													\
														\
	vector->mem_malloc_func = NULL;										\
	vector->mem_realloc_func = NULL;									\
	vector->mem_free_func = NULL;										\
}														\
														\
void	trx_vector_ ## __id ## _append(trx_vector_ ## __id ## _t *vector, __type value)				\
{														\
	__vector_ ## __id ## _ensure_free_space(vector);							\
	vector->values[vector->values_num++] = value;								\
}														\
														\
void	trx_vector_ ## __id ## _append_ptr(trx_vector_ ## __id ## _t *vector, __type *value)			\
{														\
	__vector_ ## __id ## _ensure_free_space(vector);							\
	vector->values[vector->values_num++] = *value;								\
}														\
														\
void	trx_vector_ ## __id ## _append_array(trx_vector_ ## __id ## _t *vector, __type const *values,		\
									int values_num)				\
{														\
	trx_vector_ ## __id ## _reserve(vector, vector->values_num + values_num);				\
	memcpy(vector->values + vector->values_num, values, values_num * sizeof(__type));			\
	vector->values_num = vector->values_num + values_num;							\
}														\
														\
void	trx_vector_ ## __id ## _remove_noorder(trx_vector_ ## __id ## _t *vector, int index)			\
{														\
	if (!(0 <= index && index < vector->values_num))							\
	{													\
		treegix_log(LOG_LEVEL_CRIT, "removing a non-existent element at index %d", index);		\
		exit(EXIT_FAILURE);										\
	}													\
														\
	vector->values[index] = vector->values[--vector->values_num];						\
}														\
														\
void	trx_vector_ ## __id ## _remove(trx_vector_ ## __id ## _t *vector, int index)				\
{														\
	if (!(0 <= index && index < vector->values_num))							\
	{													\
		treegix_log(LOG_LEVEL_CRIT, "removing a non-existent element at index %d", index);		\
		exit(EXIT_FAILURE);										\
	}													\
														\
	vector->values_num--;											\
	memmove(&vector->values[index], &vector->values[index + 1],						\
			sizeof(__type) * (vector->values_num - index));						\
}														\
														\
void	trx_vector_ ## __id ## _sort(trx_vector_ ## __id ## _t *vector, trx_compare_func_t compare_func)	\
{														\
	if (2 <= vector->values_num)										\
		qsort(vector->values, vector->values_num, sizeof(__type), compare_func);			\
}														\
														\
void	trx_vector_ ## __id ## _uniq(trx_vector_ ## __id ## _t *vector, trx_compare_func_t compare_func)	\
{														\
	if (2 <= vector->values_num)										\
	{													\
		int	i, j = 1;										\
														\
		for (i = 1; i < vector->values_num; i++)							\
		{												\
			if (0 != compare_func(&vector->values[i - 1], &vector->values[i]))			\
				vector->values[j++] = vector->values[i];					\
		}												\
														\
		vector->values_num = j;										\
	}													\
}														\
														\
int	trx_vector_ ## __id ## _nearestindex(const trx_vector_ ## __id ## _t *vector, const __type value,	\
									trx_compare_func_t compare_func)	\
{														\
	int	lo = 0, hi = vector->values_num, mid, c;							\
														\
	while (1 <= hi - lo)											\
	{													\
		mid = (lo + hi) / 2;										\
														\
		c = compare_func(&vector->values[mid], &value);							\
														\
		if (0 > c)											\
		{												\
			lo = mid + 1;										\
		}												\
		else if (0 == c)										\
		{												\
			return mid;										\
		}												\
		else												\
			hi = mid;										\
	}													\
														\
	return hi;												\
}														\
														\
int	trx_vector_ ## __id ## _bsearch(const trx_vector_ ## __id ## _t *vector, const __type value,		\
									trx_compare_func_t compare_func)	\
{														\
	__type	*ptr;												\
														\
	ptr = (__type *)trx_bsearch(&value, vector->values, vector->values_num, sizeof(__type), compare_func);	\
														\
	if (NULL != ptr)											\
		return (int)(ptr - vector->values);								\
	else													\
		return FAIL;											\
}														\
														\
int	trx_vector_ ## __id ## _lsearch(const trx_vector_ ## __id ## _t *vector, const __type value, int *index,\
									trx_compare_func_t compare_func)	\
{														\
	while (*index < vector->values_num)									\
	{													\
		int	c = compare_func(&vector->values[*index], &value);					\
														\
		if (0 > c)											\
		{												\
			(*index)++;										\
			continue;										\
		}												\
														\
		if (0 == c)											\
			return SUCCEED;										\
														\
		if (0 < c)											\
			break;											\
	}													\
														\
	return FAIL;												\
}														\
														\
int	trx_vector_ ## __id ## _search(const trx_vector_ ## __id ## _t *vector, const __type value,		\
									trx_compare_func_t compare_func)	\
{														\
	int	index;												\
														\
	for (index = 0; index < vector->values_num; index++)							\
	{													\
		if (0 == compare_func(&vector->values[index], &value))						\
			return index;										\
	}													\
														\
	return FAIL;												\
}														\
														\
														\
void	trx_vector_ ## __id ## _setdiff(trx_vector_ ## __id ## _t *left, const trx_vector_ ## __id ## _t *right,\
									trx_compare_func_t compare_func)	\
{														\
	int	c, block_start, deleted = 0, left_index = 0, right_index = 0;					\
														\
	while (left_index < left->values_num && right_index < right->values_num)				\
	{													\
		c = compare_func(&left->values[left_index], &right->values[right_index]);			\
														\
		if (0 >= c)											\
			left_index++;										\
														\
		if (0 <= c)											\
			right_index++;										\
														\
		if (0 != c)											\
			continue;										\
														\
		if (0 < deleted++)										\
		{												\
			memmove(&left->values[block_start - deleted + 1], &left->values[block_start],		\
							(left_index - 1 - block_start) * sizeof(__type));	\
		}												\
														\
		block_start = left_index;									\
	}													\
														\
	if (0 < deleted)											\
	{													\
		memmove(&left->values[block_start - deleted], &left->values[block_start],			\
							(left->values_num - block_start) * sizeof(__type));	\
		left->values_num -= deleted;									\
	}													\
}														\
														\
void	trx_vector_ ## __id ## _reserve(trx_vector_ ## __id ## _t *vector, size_t size)				\
{														\
	if ((int)size > vector->values_alloc)									\
	{													\
		vector->values_alloc = (int)size;								\
		vector->values = (__type *)vector->mem_realloc_func(vector->values, vector->values_alloc * sizeof(__type)); \
	}													\
}														\
														\
void	trx_vector_ ## __id ## _clear(trx_vector_ ## __id ## _t *vector)					\
{														\
	vector->values_num = 0;											\
}

#define	TRX_PTR_VECTOR_IMPL(__id, __type)									\
														\
TRX_VECTOR_IMPL(__id, __type)											\
														\
void	trx_vector_ ## __id ## _clear_ext(trx_vector_ ## __id ## _t *vector, trx_ ## __id ## _free_func_t free_func)	\
{														\
	if (0 != vector->values_num)										\
	{													\
		int	index;											\
														\
		for (index = 0; index < vector->values_num; index++)						\
			free_func(vector->values[index]);							\
														\
		vector->values_num = 0;										\
	}													\
}

#endif	/* TREEGIX_VECTORIMPL_H */
