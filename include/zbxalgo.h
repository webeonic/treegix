
#ifndef TREEGIX_TRXALGO_H
#define TREEGIX_TRXALGO_H

#include "common.h"

/* generic */

typedef trx_uint32_t trx_hash_t;

trx_hash_t	trx_hash_lookup2(const void *data, size_t len, trx_hash_t seed);
trx_hash_t	trx_hash_modfnv(const void *data, size_t len, trx_hash_t seed);
trx_hash_t	trx_hash_murmur2(const void *data, size_t len, trx_hash_t seed);
trx_hash_t	trx_hash_sdbm(const void *data, size_t len, trx_hash_t seed);
trx_hash_t	trx_hash_djb2(const void *data, size_t len, trx_hash_t seed);

#define TRX_DEFAULT_HASH_ALGO		trx_hash_modfnv
#define TRX_DEFAULT_PTR_HASH_ALGO	trx_hash_modfnv
#define TRX_DEFAULT_UINT64_HASH_ALGO	trx_hash_modfnv
#define TRX_DEFAULT_STRING_HASH_ALGO	trx_hash_modfnv

typedef trx_hash_t (*trx_hash_func_t)(const void *data);

trx_hash_t	trx_default_ptr_hash_func(const void *data);
trx_hash_t	trx_default_uint64_hash_func(const void *data);
trx_hash_t	trx_default_string_hash_func(const void *data);
trx_hash_t	trx_default_uint64_pair_hash_func(const void *data);

#define TRX_DEFAULT_HASH_SEED		0

#define TRX_DEFAULT_PTR_HASH_FUNC		trx_default_ptr_hash_func
#define TRX_DEFAULT_UINT64_HASH_FUNC		trx_default_uint64_hash_func
#define TRX_DEFAULT_STRING_HASH_FUNC		trx_default_string_hash_func
#define TRX_DEFAULT_UINT64_PAIR_HASH_FUNC	trx_default_uint64_pair_hash_func

typedef int (*trx_compare_func_t)(const void *d1, const void *d2);

int	trx_default_int_compare_func(const void *d1, const void *d2);
int	trx_default_uint64_compare_func(const void *d1, const void *d2);
int	trx_default_uint64_ptr_compare_func(const void *d1, const void *d2);
int	trx_default_str_compare_func(const void *d1, const void *d2);
int	trx_default_ptr_compare_func(const void *d1, const void *d2);
int	trx_default_uint64_pair_compare_func(const void *d1, const void *d2);

#define TRX_DEFAULT_INT_COMPARE_FUNC		trx_default_int_compare_func
#define TRX_DEFAULT_UINT64_COMPARE_FUNC		trx_default_uint64_compare_func
#define TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC	trx_default_uint64_ptr_compare_func
#define TRX_DEFAULT_STR_COMPARE_FUNC		trx_default_str_compare_func
#define TRX_DEFAULT_PTR_COMPARE_FUNC		trx_default_ptr_compare_func
#define TRX_DEFAULT_UINT64_PAIR_COMPARE_FUNC	trx_default_uint64_pair_compare_func

typedef void *(*trx_mem_malloc_func_t)(void *old, size_t size);
typedef void *(*trx_mem_realloc_func_t)(void *old, size_t size);
typedef void (*trx_mem_free_func_t)(void *ptr);

void	*trx_default_mem_malloc_func(void *old, size_t size);
void	*trx_default_mem_realloc_func(void *old, size_t size);
void	trx_default_mem_free_func(void *ptr);

#define TRX_DEFAULT_MEM_MALLOC_FUNC	trx_default_mem_malloc_func
#define TRX_DEFAULT_MEM_REALLOC_FUNC	trx_default_mem_realloc_func
#define TRX_DEFAULT_MEM_FREE_FUNC	trx_default_mem_free_func

typedef void (*trx_clean_func_t)(void *data);

#define TRX_RETURN_IF_NOT_EQUAL(a, b)	\
					\
	if ((a) < (b))			\
		return -1;		\
	if ((a) > (b))			\
		return +1

int	is_prime(int n);
int	next_prime(int n);

/* pair */

typedef struct
{
	void	*first;
	void	*second;
}
trx_ptr_pair_t;

typedef struct
{
	trx_uint64_t	first;
	trx_uint64_t	second;
}
trx_uint64_pair_t;

/* hashset */

#define TRX_HASHSET_ENTRY_T	struct trx_hashset_entry_s

TRX_HASHSET_ENTRY_T
{
	TRX_HASHSET_ENTRY_T	*next;
	trx_hash_t		hash;
#if SIZEOF_VOID_P > 4
	/* the data member must be properly aligned on 64-bit architectures that require aligned memory access */
	char			padding[sizeof(void *) - sizeof(trx_hash_t)];
#endif
	char			data[1];
};

typedef struct
{
	TRX_HASHSET_ENTRY_T	**slots;
	int			num_slots;
	int			num_data;
	trx_hash_func_t		hash_func;
	trx_compare_func_t	compare_func;
	trx_clean_func_t	clean_func;
	trx_mem_malloc_func_t	mem_malloc_func;
	trx_mem_realloc_func_t	mem_realloc_func;
	trx_mem_free_func_t	mem_free_func;
}
trx_hashset_t;

void	trx_hashset_create(trx_hashset_t *hs, size_t init_size,
				trx_hash_func_t hash_func,
				trx_compare_func_t compare_func);
void	trx_hashset_create_ext(trx_hashset_t *hs, size_t init_size,
				trx_hash_func_t hash_func,
				trx_compare_func_t compare_func,
				trx_clean_func_t clean_func,
				trx_mem_malloc_func_t mem_malloc_func,
				trx_mem_realloc_func_t mem_realloc_func,
				trx_mem_free_func_t mem_free_func);
void	trx_hashset_destroy(trx_hashset_t *hs);

int	trx_hashset_reserve(trx_hashset_t *hs, int num_slots_req);
void	*trx_hashset_insert(trx_hashset_t *hs, const void *data, size_t size);
void	*trx_hashset_insert_ext(trx_hashset_t *hs, const void *data, size_t size, size_t offset);
void	*trx_hashset_search(trx_hashset_t *hs, const void *data);
void	trx_hashset_remove(trx_hashset_t *hs, const void *data);
void	trx_hashset_remove_direct(trx_hashset_t *hs, const void *data);

void	trx_hashset_clear(trx_hashset_t *hs);

typedef struct
{
	trx_hashset_t		*hashset;
	int			slot;
	TRX_HASHSET_ENTRY_T	*entry;
}
trx_hashset_iter_t;

void	trx_hashset_iter_reset(trx_hashset_t *hs, trx_hashset_iter_t *iter);
void	*trx_hashset_iter_next(trx_hashset_iter_t *iter);
void	trx_hashset_iter_remove(trx_hashset_iter_t *iter);

/* hashmap */

/* currently, we only have a very specialized hashmap */
/* that maps trx_uint64_t keys into non-negative ints */

#define TRX_HASHMAP_ENTRY_T	struct trx_hashmap_entry_s
#define TRX_HASHMAP_SLOT_T	struct trx_hashmap_slot_s

TRX_HASHMAP_ENTRY_T
{
	trx_uint64_t	key;
	int		value;
};

TRX_HASHMAP_SLOT_T
{
	TRX_HASHMAP_ENTRY_T	*entries;
	int			entries_num;
	int			entries_alloc;
};

typedef struct
{
	TRX_HASHMAP_SLOT_T	*slots;
	int			num_slots;
	int			num_data;
	trx_hash_func_t		hash_func;
	trx_compare_func_t	compare_func;
	trx_mem_malloc_func_t	mem_malloc_func;
	trx_mem_realloc_func_t	mem_realloc_func;
	trx_mem_free_func_t	mem_free_func;
}
trx_hashmap_t;

void	trx_hashmap_create(trx_hashmap_t *hm, size_t init_size);
void	trx_hashmap_create_ext(trx_hashmap_t *hm, size_t init_size,
				trx_hash_func_t hash_func,
				trx_compare_func_t compare_func,
				trx_mem_malloc_func_t mem_malloc_func,
				trx_mem_realloc_func_t mem_realloc_func,
				trx_mem_free_func_t mem_free_func);
void	trx_hashmap_destroy(trx_hashmap_t *hm);

int	trx_hashmap_get(trx_hashmap_t *hm, trx_uint64_t key);
void	trx_hashmap_set(trx_hashmap_t *hm, trx_uint64_t key, int value);
void	trx_hashmap_remove(trx_hashmap_t *hm, trx_uint64_t key);

void	trx_hashmap_clear(trx_hashmap_t *hm);

/* binary heap (min-heap) */

/* currently, we only have a very specialized binary heap that can */
/* store trx_uint64_t keys with arbitrary auxiliary information */

#define TRX_BINARY_HEAP_OPTION_EMPTY	0
#define TRX_BINARY_HEAP_OPTION_DIRECT	(1<<0)	/* support for direct update() and remove() operations */

typedef struct
{
	trx_uint64_t		key;
	const void		*data;
}
trx_binary_heap_elem_t;

typedef struct
{
	trx_binary_heap_elem_t	*elems;
	int			elems_num;
	int			elems_alloc;
	int			options;
	trx_compare_func_t	compare_func;
	trx_hashmap_t		*key_index;

	/* The binary heap is designed to work correctly only with memory allocation functions */
	/* that return pointer to the allocated memory or quit. Functions that can return NULL */
	/* are not supported (process will exit() if NULL return value is encountered). If     */
	/* using trx_mem_info_t and the associated memory functions then ensure that allow_oom */
	/* is always set to 0.                                                                 */
	trx_mem_malloc_func_t	mem_malloc_func;
	trx_mem_realloc_func_t	mem_realloc_func;
	trx_mem_free_func_t	mem_free_func;
}
trx_binary_heap_t;

void			trx_binary_heap_create(trx_binary_heap_t *heap, trx_compare_func_t compare_func, int options);
void			trx_binary_heap_create_ext(trx_binary_heap_t *heap, trx_compare_func_t compare_func, int options,
							trx_mem_malloc_func_t mem_malloc_func,
							trx_mem_realloc_func_t mem_realloc_func,
							trx_mem_free_func_t mem_free_func);
void			trx_binary_heap_destroy(trx_binary_heap_t *heap);

int			trx_binary_heap_empty(trx_binary_heap_t *heap);
trx_binary_heap_elem_t	*trx_binary_heap_find_min(trx_binary_heap_t *heap);
void			trx_binary_heap_insert(trx_binary_heap_t *heap, trx_binary_heap_elem_t *elem);
void			trx_binary_heap_update_direct(trx_binary_heap_t *heap, trx_binary_heap_elem_t *elem);
void			trx_binary_heap_remove_min(trx_binary_heap_t *heap);
void			trx_binary_heap_remove_direct(trx_binary_heap_t *heap, trx_uint64_t key);

void			trx_binary_heap_clear(trx_binary_heap_t *heap);

/* vector */

#define TRX_VECTOR_DECL(__id, __type)										\
														\
typedef struct													\
{														\
	__type			*values;									\
	int			values_num;									\
	int			values_alloc;									\
	trx_mem_malloc_func_t	mem_malloc_func;								\
	trx_mem_realloc_func_t	mem_realloc_func;								\
	trx_mem_free_func_t	mem_free_func;									\
}														\
trx_vector_ ## __id ## _t;											\
														\
void	trx_vector_ ## __id ## _create(trx_vector_ ## __id ## _t *vector);					\
void	trx_vector_ ## __id ## _create_ext(trx_vector_ ## __id ## _t *vector,					\
						trx_mem_malloc_func_t mem_malloc_func,				\
						trx_mem_realloc_func_t mem_realloc_func,			\
						trx_mem_free_func_t mem_free_func);				\
void	trx_vector_ ## __id ## _destroy(trx_vector_ ## __id ## _t *vector);					\
														\
void	trx_vector_ ## __id ## _append(trx_vector_ ## __id ## _t *vector, __type value);			\
void	trx_vector_ ## __id ## _append_ptr(trx_vector_ ## __id ## _t *vector, __type *value);			\
void	trx_vector_ ## __id ## _append_array(trx_vector_ ## __id ## _t *vector, __type const *values,		\
									int values_num);			\
void	trx_vector_ ## __id ## _remove_noorder(trx_vector_ ## __id ## _t *vector, int index);			\
void	trx_vector_ ## __id ## _remove(trx_vector_ ## __id ## _t *vector, int index);				\
														\
void	trx_vector_ ## __id ## _sort(trx_vector_ ## __id ## _t *vector, trx_compare_func_t compare_func);	\
void	trx_vector_ ## __id ## _uniq(trx_vector_ ## __id ## _t *vector, trx_compare_func_t compare_func);	\
														\
int	trx_vector_ ## __id ## _nearestindex(const trx_vector_ ## __id ## _t *vector, const __type value,	\
									trx_compare_func_t compare_func);	\
int	trx_vector_ ## __id ## _bsearch(const trx_vector_ ## __id ## _t *vector, const __type value,		\
									trx_compare_func_t compare_func);	\
int	trx_vector_ ## __id ## _lsearch(const trx_vector_ ## __id ## _t *vector, const __type value, int *index,\
									trx_compare_func_t compare_func);	\
int	trx_vector_ ## __id ## _search(const trx_vector_ ## __id ## _t *vector, const __type value,		\
									trx_compare_func_t compare_func);	\
void	trx_vector_ ## __id ## _setdiff(trx_vector_ ## __id ## _t *left, const trx_vector_ ## __id ## _t *right,\
									trx_compare_func_t compare_func);	\
														\
void	trx_vector_ ## __id ## _reserve(trx_vector_ ## __id ## _t *vector, size_t size);			\
void	trx_vector_ ## __id ## _clear(trx_vector_ ## __id ## _t *vector);

#define TRX_PTR_VECTOR_DECL(__id, __type)									\
														\
TRX_VECTOR_DECL(__id, __type)											\
														\
typedef void (*trx_ ## __id ## _free_func_t)(__type data);							\
														\
void	trx_vector_ ## __id ## _clear_ext(trx_vector_ ## __id ## _t *vector, trx_ ## __id ## _free_func_t free_func);

TRX_VECTOR_DECL(uint64, trx_uint64_t)
TRX_PTR_VECTOR_DECL(str, char *)
TRX_PTR_VECTOR_DECL(ptr, void *)
TRX_VECTOR_DECL(ptr_pair, trx_ptr_pair_t)
TRX_VECTOR_DECL(uint64_pair, trx_uint64_pair_t)

/* this function is only for use with trx_vector_XXX_clear_ext() */
/* and only if the vector does not contain nested allocations */
void	trx_ptr_free(void *data);
void	trx_str_free(char *data);

/* 128 bit unsigned integer handling */
#define uset128(base, hi64, lo64)	(base)->hi = hi64; (base)->lo = lo64

void	uinc128_64(trx_uint128_t *base, trx_uint64_t value);
void	uinc128_128(trx_uint128_t *base, const trx_uint128_t *value);
void	udiv128_64(trx_uint128_t *result, const trx_uint128_t *base, trx_uint64_t value);
void	umul64_64(trx_uint128_t *result, trx_uint64_t value, trx_uint64_t factor);

unsigned int	trx_isqrt32(unsigned int value);

/* expression evaluation */

#define TRX_UNKNOWN_STR		"TRX_UNKNOWN"	/* textual representation of TRX_UNKNOWN */
#define TRX_UNKNOWN_STR_LEN	TRX_CONST_STRLEN(TRX_UNKNOWN_STR)

int	evaluate(double *value, const char *expression, char *error, size_t max_error_len,
		trx_vector_ptr_t *unknown_msgs);

/* forecasting */

#define TRX_MATH_ERROR	-1.0

typedef enum
{
	FIT_LINEAR,
	FIT_POLYNOMIAL,
	FIT_EXPONENTIAL,
	FIT_LOGARITHMIC,
	FIT_POWER,
	FIT_INVALID
}
trx_fit_t;

typedef enum
{
	MODE_VALUE,
	MODE_MAX,
	MODE_MIN,
	MODE_DELTA,
	MODE_AVG,
	MODE_INVALID
}
trx_mode_t;

int	trx_fit_code(char *fit_str, trx_fit_t *fit, unsigned *k, char **error);
int	trx_mode_code(char *mode_str, trx_mode_t *mode, char **error);
double	trx_forecast(double *t, double *x, int n, double now, double time, trx_fit_t fit, unsigned k, trx_mode_t mode);
double	trx_timeleft(double *t, double *x, int n, double now, double threshold, trx_fit_t fit, unsigned k);


/* fifo queue of pointers */

typedef struct
{
	void	**values;
	int	alloc_num;
	int	head_pos;
	int	tail_pos;
}
trx_queue_ptr_t;

#define trx_queue_ptr_empty(queue)	((queue)->head_pos == (queue)->tail_pos ? SUCCEED : FAIL)

int	trx_queue_ptr_values_num(trx_queue_ptr_t *queue);
void	trx_queue_ptr_reserve(trx_queue_ptr_t *queue, int num);
void	trx_queue_ptr_compact(trx_queue_ptr_t *queue);
void	trx_queue_ptr_create(trx_queue_ptr_t *queue);
void	trx_queue_ptr_destroy(trx_queue_ptr_t *queue);
void	trx_queue_ptr_push(trx_queue_ptr_t *queue, void *value);
void	*trx_queue_ptr_pop(trx_queue_ptr_t *queue);
void	trx_queue_ptr_remove_value(trx_queue_ptr_t *queue, const void *value);



#endif
