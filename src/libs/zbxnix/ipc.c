

#include "common.h"
#include "ipc.h"
#include "log.h"

/******************************************************************************
 *                                                                            *
 * Function: zbx_shm_create                                                   *
 *                                                                            *
 * Purpose: Create block of shared memory                                     *
 *                                                                            *
 * Parameters:  size - size                                                   *
 *                                                                            *
 * Return value: If the function succeeds, then return SHM ID                 *
 *               -1 on an error                                               *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
int	zbx_shm_create(size_t size)
{
	int	shm_id;

	if (-1 == (shm_id = shmget(IPC_PRIVATE, size, IPC_CREAT | IPC_EXCL | 0600)))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot allocate shared memory of size " TRX_FS_SIZE_T ": %s",
				(zbx_fs_size_t)size, zbx_strerror(errno));
		return -1;
	}

	return shm_id;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_shm_destroy                                                  *
 *                                                                            *
 * Purpose: Destroy block of shared memory                                    *
 *                                                                            *
 * Parameters:  shmid - Shared memory identifier                              *
 *                                                                            *
 * Return value: If the function succeeds, then return 0                      *
 *               -1 on an error                                               *
 *                                                                            *
 * Author: Andrea Biscuola                                                    *
 *                                                                            *
 ******************************************************************************/
int	zbx_shm_destroy(int shmid)
{
	if (-1 == shmctl(shmid, IPC_RMID, 0))
	{
		zbx_error("cannot remove existing shared memory: %s", zbx_strerror(errno));
		return -1;
	}

	return 0;
}
