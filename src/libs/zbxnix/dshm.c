

#include "common.h"
#include "ipc.h"
#include "log.h"

extern char	*CONFIG_FILE;

/******************************************************************************
 *                                                                            *
 * Function: trx_dshm_create                                                  *
 *                                                                            *
 * Purpose: creates dynamic shared memory segment                             *
 *                                                                            *
 * Parameters: shm       - [OUT] the dynamic shared memory data               *
 *             shm_size  - [IN] the initial size (can be 0)                   *
 *             mutex     - [IN] the name of mutex used to synchronize memory  *
 *                              access                                        *
 *             copy_func - [IN] the function used to copy shared memory       *
 *                              contents during reallocation                  *
 *             errmsg    - [OUT] the error message                            *
 *                                                                            *
 * Return value: SUCCEED - the dynamic shared memory segment was created      *
 *                         successfully.                                      *
 *               FAIL    - otherwise. The errmsg contains error message and   *
 *                         must be freed by the caller.                       *
 *                                                                            *
 ******************************************************************************/
int	trx_dshm_create(trx_dshm_t *shm, size_t shm_size, trx_mutex_name_t mutex,
		trx_shm_copy_func_t copy_func, char **errmsg)
{
	int	ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() size:" TRX_FS_SIZE_T, __func__, (trx_fs_size_t)shm_size);

	if (SUCCEED != trx_mutex_create(&shm->lock, mutex, errmsg))
		goto out;

	if (0 < shm_size)
	{
		if (-1 == (shm->shmid = trx_shm_create(shm_size)))
		{
			*errmsg = trx_strdup(*errmsg, "cannot allocate shared memory");
			goto out;
		}
	}
	else
		shm->shmid = TRX_NONEXISTENT_SHMID;

	shm->size = shm_size;
	shm->copy_func = copy_func;

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s shmid:%d", __func__, trx_result_string(ret), shm->shmid);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dshm_destroy                                                 *
 *                                                                            *
 * Purpose: destroys dynamic shared memory segment                            *
 *                                                                            *
 * Parameters: shm    - [IN] the dynamic shared memory data                   *
 *             errmsg - [OUT] the error message                               *
 *                                                                            *
 * Return value: SUCCEED - the dynamic shared memory segment was destroyed    *
 *                         successfully.                                      *
 *               FAIL    - otherwise. The errmsg contains error message and   *
 *                         must be freed by the caller.                       *
 *                                                                            *
 ******************************************************************************/
int	trx_dshm_destroy(trx_dshm_t *shm, char **errmsg)
{
	int	ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() shmid:%d", __func__, shm->shmid);

	trx_mutex_destroy(&shm->lock);

	if (TRX_NONEXISTENT_SHMID != shm->shmid)
	{
		if (-1 == shmctl(shm->shmid, IPC_RMID, NULL))
		{
			*errmsg = trx_dsprintf(*errmsg, "cannot remove shared memory: %s", trx_strerror(errno));
			goto out;
		}
		shm->shmid = TRX_NONEXISTENT_SHMID;
	}

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dshm_lock                                                    *
 *                                                                            *
 ******************************************************************************/
void	trx_dshm_lock(trx_dshm_t *shm)
{
	trx_mutex_lock(shm->lock);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dshm_unlock                                                  *
 *                                                                            *
 ******************************************************************************/
void	trx_dshm_unlock(trx_dshm_t *shm)
{
	trx_mutex_unlock(shm->lock);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dshm_validate_ref                                            *
 *                                                                            *
 * Purpose: validates local reference to dynamic shared memory segment        *
 *                                                                            *
 * Parameters: shm     - [IN] the dynamic shared memory data                  *
 *             shm_ref - [IN/OUT] a local reference to dynamic shared memory  *
 *                                segment                                     *
 *             errmsg  - [OUT] the error message                              *
 *                                                                            *
 * Return value: SUCCEED - the local reference to dynamic shared memory       *
 *                         segment was validated successfully and contains    *
 *                         correct dynamic shared memory segment address      *
 *               FAIL    - otherwise. The errmsg contains error message and   *
 *                         must be freed by the caller.                       *
 *                                                                            *
 * Comments: This function should be called before accessing the dynamic      *
 *           shared memory to ensure that the local reference has correct     *
 *           address after shared memory allocation/reallocation.             *
 *                                                                            *
 ******************************************************************************/
int	trx_dshm_validate_ref(const trx_dshm_t *shm, trx_dshm_ref_t *shm_ref, char **errmsg)
{
	int	ret = FAIL;

	treegix_log(LOG_LEVEL_TRACE, "In %s() shmid:%d refid:%d", __func__, shm->shmid, shm_ref->shmid);

	if (shm->shmid != shm_ref->shmid)
	{
		if (TRX_NONEXISTENT_SHMID != shm_ref->shmid)
		{
			if (-1 == shmdt((void *)shm_ref->addr))
			{
				*errmsg = trx_dsprintf(*errmsg, "cannot detach shared memory: %s", trx_strerror(errno));
				goto out;
			}
			shm_ref->addr = NULL;
			shm_ref->shmid = TRX_NONEXISTENT_SHMID;
		}

		if ((void *)(-1) == (shm_ref->addr = shmat(shm->shmid, NULL, 0)))
		{
			*errmsg = trx_dsprintf(*errmsg, "cannot attach shared memory: %s", trx_strerror(errno));
			shm_ref->addr = NULL;
			goto out;
		}

		shm_ref->shmid = shm->shmid;
	}

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_TRACE, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_dshm_realloc                                                 *
 *                                                                            *
 * Purpose: reallocates dynamic shared memory segment                         *
 *                                                                            *
 * Parameters: shm      - [IN/OUT] the dynamic shared memory data             *
 *             size     - [IN] the new segment size                           *
 *             errmsg   - [OUT] the error message                             *
 *                                                                            *
 * Return value:                                                              *
 *    SUCCEED - the shared memory segment was successfully reallocated.       *
 *    FAIL    - otherwise. The errmsg contains error message and must be      *
 *              freed by the caller.                                          *
 *                                                                            *
 * Comments: The shared memory segment is reallocated by simply creating      *
 *           a new segment and copying the data from old segment by calling   *
 *           the copy_data callback function.                                 *
 *                                                                            *
 ******************************************************************************/
int	trx_dshm_realloc(trx_dshm_t *shm, size_t size, char **errmsg)
{
	int	shmid, ret = FAIL;
	void	*addr, *addr_old = NULL;
	size_t	shm_size;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() shmid:%d size:" TRX_FS_SIZE_T, __func__, shm->shmid, (trx_fs_size_t)size);

	shm_size = TRX_SIZE_T_ALIGN8(size);

	/* attach to the old segment if possible */
	if (TRX_NONEXISTENT_SHMID != shm->shmid && (void *)(-1) == (addr_old = shmat(shm->shmid, NULL, 0)))
	{
		*errmsg = trx_dsprintf(*errmsg, "cannot attach current shared memory: %s", trx_strerror(errno));
		goto out;
	}

	if (-1 == (shmid = trx_shm_create(shm_size)))
	{
		*errmsg = trx_strdup(NULL, "cannot allocate shared memory");
		goto out;
	}

	if ((void *)(-1) == (addr = shmat(shmid, NULL, 0)))
	{
		if (NULL != addr_old)
			(void)shmdt(addr_old);

		*errmsg = trx_dsprintf(*errmsg, "cannot attach new shared memory: %s", trx_strerror(errno));
		goto out;
	}

	/* copy data from the old segment */
	shm->copy_func(addr, shm_size, addr_old);

	if (-1 == shmdt((void *)addr))
	{
		*errmsg = trx_strdup(*errmsg, "cannot detach from new shared memory");
		goto out;
	}

	/* delete the old segment */
	if (NULL != addr_old && -1 == trx_shm_destroy(shm->shmid))
	{
		*errmsg = trx_strdup(*errmsg, "cannot detach from old shared memory");
		goto out;
	}

	shm->size = shm_size;
	shm->shmid = shmid;

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s shmid:%d", __func__, trx_result_string(ret), shm->shmid);

	return ret;
}
