
#ifndef TREEGIX_IPC_H
#define TREEGIX_IPC_H

#if defined(_WINDOWS)
#	error "This module allowed only for Unix OS"
#endif

#include "mutexs.h"

#define TRX_NONEXISTENT_SHMID		(-1)

int	trx_shm_create(size_t size);
int	trx_shm_destroy(int shmid);

/* data copying callback function prototype */
typedef void (*trx_shm_copy_func_t)(void *dst, size_t size_dst, const void *src);

/* dynamic shared memory data structure */
typedef struct
{
	/* shared memory segment identifier */
	int			shmid;

	/* allocated size */
	size_t			size;

	/* callback function to copy data after shared memory reallocation */
	trx_shm_copy_func_t	copy_func;

	trx_mutex_t		lock;
}
trx_dshm_t;

/* local process reference to dynamic shared memory data */
typedef struct
{
	/* shared memory segment identifier */
	int	shmid;

	/* shared memory base address */
	void	*addr;
}
trx_dshm_ref_t;

int	trx_dshm_create(trx_dshm_t *shm, size_t shm_size, trx_mutex_name_t mutex,
		trx_shm_copy_func_t copy_func, char **errmsg);

int	trx_dshm_destroy(trx_dshm_t *shm, char **errmsg);

int	trx_dshm_realloc(trx_dshm_t *shm, size_t size, char **errmsg);

int	trx_dshm_validate_ref(const trx_dshm_t *shm, trx_dshm_ref_t *shm_ref, char **errmsg);

void	trx_dshm_lock(trx_dshm_t *shm);
void	trx_dshm_unlock(trx_dshm_t *shm);

#endif
