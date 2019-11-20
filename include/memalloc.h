

#ifndef TREEGIX_MEMALLOC_H
#define TREEGIX_MEMALLOC_H

#include "common.h"
#include "mutexs.h"

typedef struct
{
	void		**buckets;
	void		*lo_bound;
	void		*hi_bound;
	trx_uint64_t	free_size;
	trx_uint64_t	used_size;
	trx_uint64_t	orig_size;
	trx_uint64_t	total_size;
	int		shm_id;

	/* Continue execution in out of memory situation.                         */
	/* Normally allocator forces exit when it runs out of allocatable memory. */
	/* Set this flag to 1 to allow execution in out of memory situations.     */
	char		allow_oom;

	const char	*mem_descr;
	const char	*mem_param;
}
trx_mem_info_t;

int	trx_mem_create(trx_mem_info_t **info, trx_uint64_t size, const char *descr, const char *param, int allow_oom,
		char **error);

#define	trx_mem_malloc(info, old, size) __trx_mem_malloc(__FILE__, __LINE__, info, old, size)
#define	trx_mem_realloc(info, old, size) __trx_mem_realloc(__FILE__, __LINE__, info, old, size)
#define	trx_mem_free(info, ptr)				\
							\
do							\
{							\
	__trx_mem_free(__FILE__, __LINE__, info, ptr);	\
	ptr = NULL;					\
}							\
while (0)

void	*__trx_mem_malloc(const char *file, int line, trx_mem_info_t *info, const void *old, size_t size);
void	*__trx_mem_realloc(const char *file, int line, trx_mem_info_t *info, void *old, size_t size);
void	__trx_mem_free(const char *file, int line, trx_mem_info_t *info, void *ptr);

void	trx_mem_clear(trx_mem_info_t *info);

void	trx_mem_dump_stats(int level, trx_mem_info_t *info);

size_t	trx_mem_required_size(int chunks_num, const char *descr, const char *param);

#define TRX_MEM_FUNC1_DECL_MALLOC(__prefix)				\
static void	*__prefix ## _mem_malloc_func(void *old, size_t size)
#define TRX_MEM_FUNC1_DECL_REALLOC(__prefix)				\
static void	*__prefix ## _mem_realloc_func(void *old, size_t size)
#define TRX_MEM_FUNC1_DECL_FREE(__prefix)				\
static void	__prefix ## _mem_free_func(void *ptr)

#define TRX_MEM_FUNC1_IMPL_MALLOC(__prefix, __info)			\
									\
static void	*__prefix ## _mem_malloc_func(void *old, size_t size)	\
{									\
	return trx_mem_malloc(__info, old, size);			\
}

#define TRX_MEM_FUNC1_IMPL_REALLOC(__prefix, __info)			\
									\
static void	*__prefix ## _mem_realloc_func(void *old, size_t size)	\
{									\
	return trx_mem_realloc(__info, old, size);			\
}

#define TRX_MEM_FUNC1_IMPL_FREE(__prefix, __info)			\
									\
static void	__prefix ## _mem_free_func(void *ptr)			\
{									\
	trx_mem_free(__info, ptr);					\
}

#define TRX_MEM_FUNC_DECL(__prefix)					\
									\
TRX_MEM_FUNC1_DECL_MALLOC(__prefix);					\
TRX_MEM_FUNC1_DECL_REALLOC(__prefix);					\
TRX_MEM_FUNC1_DECL_FREE(__prefix);

#define TRX_MEM_FUNC_IMPL(__prefix, __info)				\
									\
TRX_MEM_FUNC1_IMPL_MALLOC(__prefix, __info)				\
TRX_MEM_FUNC1_IMPL_REALLOC(__prefix, __info)				\
TRX_MEM_FUNC1_IMPL_FREE(__prefix, __info)

#endif
