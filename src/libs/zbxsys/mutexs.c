

#include "common.h"
#include "log.h"
#include "mutexs.h"

#ifdef _WINDOWS
#	include "sysinfo.h"
#else
#ifdef HAVE_PTHREAD_PROCESS_SHARED
typedef struct
{
	pthread_mutex_t		mutexes[TRX_MUTEX_COUNT];
	pthread_rwlock_t	rwlocks[TRX_RWLOCK_COUNT];
}
trx_shared_lock_t;

static trx_shared_lock_t	*shared_lock;
static int			shm_id, locks_disabled;
#else
#	if !HAVE_SEMUN
		union semun
		{
			int			val;	/* value for SETVAL */
			struct semid_ds		*buf;	/* buffer for IPC_STAT & IPC_SET */
			unsigned short int	*array;	/* array for GETALL & SETALL */
			struct seminfo		*__buf;	/* buffer for IPC_INFO */
		};

#		undef HAVE_SEMUN
#		define HAVE_SEMUN 1
#	endif	/* HAVE_SEMUN */

#	include "cfg.h"
#	include "threads.h"

	static int		TRX_SEM_LIST_ID;
	static unsigned char	mutexes;
#endif

/******************************************************************************
 *                                                                            *
 * Function: trx_locks_create                                                 *
 *                                                                            *
 * Purpose: if pthread mutexes and read-write locks can be shared between     *
 *          processes then create them, otherwise fallback to System V        *
 *          semaphore operations                                              *
 *                                                                            *
 * Parameters: error - dynamically allocated memory with error message.       *
 *                                                                            *
 * Return value: SUCCEED if mutexes successfully created, otherwise FAIL      *
 *                                                                            *
 ******************************************************************************/
int	trx_locks_create(char **error)
{
#ifdef HAVE_PTHREAD_PROCESS_SHARED
	int			i;
	pthread_mutexattr_t	mta;
	pthread_rwlockattr_t	rwa;

	if (-1 == (shm_id = shmget(IPC_PRIVATE, TRX_SIZE_T_ALIGN8(sizeof(trx_shared_lock_t)),
			IPC_CREAT | IPC_EXCL | 0600)))
	{
		*error = trx_dsprintf(*error, "cannot allocate shared memory for locks");
		return FAIL;
	}

	if ((void *)(-1) == (shared_lock = (trx_shared_lock_t *)shmat(shm_id, NULL, 0)))
	{
		*error = trx_dsprintf(*error, "cannot attach shared memory for locks: %s", trx_strerror(errno));
		return FAIL;
	}

	memset(shared_lock, 0, sizeof(trx_shared_lock_t));

	/* immediately mark the new shared memory for destruction after attaching to it */
	if (-1 == shmctl(shm_id, IPC_RMID, 0))
	{
		*error = trx_dsprintf(*error, "cannot mark the new shared memory for destruction: %s",
				trx_strerror(errno));
		return FAIL;
	}

	if (0 != pthread_mutexattr_init(&mta))
	{
		*error = trx_dsprintf(*error, "cannot initialize mutex attribute: %s", trx_strerror(errno));
		return FAIL;
	}

	if (0 != pthread_mutexattr_setpshared(&mta, PTHREAD_PROCESS_SHARED))
	{
		*error = trx_dsprintf(*error, "cannot set shared mutex attribute: %s", trx_strerror(errno));
		return FAIL;
	}

	for (i = 0; i < TRX_MUTEX_COUNT; i++)
	{
		if (0 != pthread_mutex_init(&shared_lock->mutexes[i], &mta))
		{
			*error = trx_dsprintf(*error, "cannot create mutex: %s", trx_strerror(errno));
			return FAIL;
		}
	}

	if (0 != pthread_rwlockattr_init(&rwa))
	{
		*error = trx_dsprintf(*error, "cannot initialize read write lock attribute: %s", trx_strerror(errno));
		return FAIL;
	}

	if (0 != pthread_rwlockattr_setpshared(&rwa, PTHREAD_PROCESS_SHARED))
	{
		*error = trx_dsprintf(*error, "cannot set shared read write lock attribute: %s", trx_strerror(errno));
		return FAIL;
	}

	for (i = 0; i < TRX_RWLOCK_COUNT; i++)
	{
		if (0 != pthread_rwlock_init(&shared_lock->rwlocks[i], &rwa))
		{
			*error = trx_dsprintf(*error, "cannot create rwlock: %s", trx_strerror(errno));
			return FAIL;
		}
	}
#else
	union semun	semopts;
	int		i;

	if (-1 == (TRX_SEM_LIST_ID = semget(IPC_PRIVATE, TRX_MUTEX_COUNT + TRX_RWLOCK_COUNT, 0600)))
	{
		*error = trx_dsprintf(*error, "cannot create semaphore set: %s", trx_strerror(errno));
		return FAIL;
	}

	/* set default semaphore value */

	semopts.val = 1;
	for (i = 0; TRX_MUTEX_COUNT + TRX_RWLOCK_COUNT > i; i++)
	{
		if (-1 != semctl(TRX_SEM_LIST_ID, i, SETVAL, semopts))
			continue;

		*error = trx_dsprintf(*error, "cannot initialize semaphore: %s", trx_strerror(errno));

		if (-1 == semctl(TRX_SEM_LIST_ID, 0, IPC_RMID, 0))
			trx_error("cannot remove semaphore set %d: %s", TRX_SEM_LIST_ID, trx_strerror(errno));

		TRX_SEM_LIST_ID = -1;

		return FAIL;
	}
#endif
	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_rwlock_create                                                *
 *                                                                            *
 * Purpose: read-write locks are created using trx_locks_create() function    *
 *          this is only to obtain handle, if read write locks are not        *
 *          supported, then outputs numeric handle of mutex that can be used  *
 *          with mutex handling functions                                     *
 *                                                                            *
 * Parameters:  rwlock - read-write lock handle if supported, otherwise mutex *
 *              name - name of read-write lock (index for nix system)         *
 *              error - unused                                                *
 *                                                                            *
 * Return value: SUCCEED if mutexes successfully created, otherwise FAIL      *
 *                                                                            *
 ******************************************************************************/
int	trx_rwlock_create(trx_rwlock_t *rwlock, trx_rwlock_name_t name, char **error)
{
	TRX_UNUSED(error);
#ifdef HAVE_PTHREAD_PROCESS_SHARED
	*rwlock = &shared_lock->rwlocks[name];
#else
	*rwlock = name + TRX_MUTEX_COUNT;
	mutexes++;
#endif
	return SUCCEED;
}
#ifdef HAVE_PTHREAD_PROCESS_SHARED
/******************************************************************************
 *                                                                            *
 * Function: __trx_rwlock_wrlock                                              *
 *                                                                            *
 * Purpose: acquire write lock for read-write lock (exclusive access)         *
 *                                                                            *
 * Parameters: rwlock - handle of read-write lock                             *
 *                                                                            *
 ******************************************************************************/
void	__trx_rwlock_wrlock(const char *filename, int line, trx_rwlock_t rwlock)
{
	if (TRX_RWLOCK_NULL == rwlock)
		return;

	if (0 != locks_disabled)
		return;

	if (0 != pthread_rwlock_wrlock(rwlock))
	{
		trx_error("[file:'%s',line:%d] write lock failed: %s", filename, line, trx_strerror(errno));
		exit(EXIT_FAILURE);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: __trx_rwlock_rdlock                                              *
 *                                                                            *
 * Purpose: acquire read lock for read-write lock (there can be many readers) *
 *                                                                            *
 * Parameters: rwlock - handle of read-write lock                             *
 *                                                                            *
 ******************************************************************************/
void	__trx_rwlock_rdlock(const char *filename, int line, trx_rwlock_t rwlock)
{
	if (TRX_RWLOCK_NULL == rwlock)
		return;

	if (0 != locks_disabled)
		return;

	if (0 != pthread_rwlock_rdlock(rwlock))
	{
		trx_error("[file:'%s',line:%d] read lock failed: %s", filename, line, trx_strerror(errno));
		exit(EXIT_FAILURE);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: __trx_rwlock_unlock                                              *
 *                                                                            *
 * Purpose: unlock read-write lock                                            *
 *                                                                            *
 * Parameters: rwlock - handle of read-write lock                             *
 *                                                                            *
 ******************************************************************************/
void	__trx_rwlock_unlock(const char *filename, int line, trx_rwlock_t rwlock)
{
	if (TRX_RWLOCK_NULL == rwlock)
		return;

	if (0 != locks_disabled)
		return;

	if (0 != pthread_rwlock_unlock(rwlock))
	{
		trx_error("[file:'%s',line:%d] read-write lock unlock failed: %s", filename, line, trx_strerror(errno));
		exit(EXIT_FAILURE);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: trx_rwlock_destroy                                               *
 *                                                                            *
 * Purpose: Destroy read-write lock                                           *
 *                                                                            *
 * Parameters: rwlock - handle of read-write lock                             *
 *                                                                            *
 *                                                                            *
 ******************************************************************************/

void	trx_rwlock_destroy(trx_rwlock_t *rwlock)
{
	if (TRX_RWLOCK_NULL == *rwlock)
		return;

	if (0 != locks_disabled)
		return;

	if (0 != pthread_rwlock_destroy(*rwlock))
		trx_error("cannot remove read-write lock: %s", trx_strerror(errno));

	*rwlock = TRX_RWLOCK_NULL;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_locks_disable                                                *
 *                                                                            *
 * Purpose:  disable locks                                                    *
 *                                                                            *
 ******************************************************************************/
void	trx_locks_disable(void)
{
	/* attempting to destroy a locked pthread mutex results in undefined behavior */
	locks_disabled = 1;
}
#endif
#endif	/* _WINDOWS */

/******************************************************************************
 *                                                                            *
 * Function: trx_mutex_create                                                 *
 *                                                                            *
 * Purpose: Create the mutex                                                  *
 *                                                                            *
 * Parameters:  mutex - handle of mutex                                       *
 *              name - name of mutex (index for nix system)                   *
 *                                                                            *
 * Return value: If the function succeeds, then return SUCCEED,               *
 *               FAIL on an error                                             *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
int	trx_mutex_create(trx_mutex_t *mutex, trx_mutex_name_t name, char **error)
{
#ifdef _WINDOWS
	if (NULL == (*mutex = CreateMutex(NULL, FALSE, name)))
	{
		*error = trx_dsprintf(*error, "error on mutex creating: %s", strerror_from_system(GetLastError()));
		return FAIL;
	}
#else
	TRX_UNUSED(error);
#ifdef	HAVE_PTHREAD_PROCESS_SHARED
	*mutex = &shared_lock->mutexes[name];
#else
	mutexes++;
	*mutex = name;
#endif
#endif
	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_mutex_lock                                                   *
 *                                                                            *
 * Purpose: Waits until the mutex is in the signalled state                   *
 *                                                                            *
 * Parameters: mutex - handle of mutex                                        *
 *                                                                            *
 * Author: Eugene Grigorjev, Alexander Vladishev                              *
 *                                                                            *
 ******************************************************************************/
void	__trx_mutex_lock(const char *filename, int line, trx_mutex_t mutex)
{
#ifndef _WINDOWS
#ifndef	HAVE_PTHREAD_PROCESS_SHARED
	struct sembuf	sem_lock;
#endif
#else
	DWORD   dwWaitResult;
#endif

	if (TRX_MUTEX_NULL == mutex)
		return;

#ifdef _WINDOWS
#ifdef TREEGIX_AGENT
	if (0 != (TRX_MUTEX_THREAD_DENIED & get_thread_global_mutex_flag()))
	{
		trx_error("[file:'%s',line:%d] lock failed: TRX_MUTEX_THREAD_DENIED is set for thread with id = %d",
				filename, line, trx_get_thread_id());
		exit(EXIT_FAILURE);
	}
#endif
	dwWaitResult = WaitForSingleObject(mutex, INFINITE);

	switch (dwWaitResult)
	{
		case WAIT_OBJECT_0:
			break;
		case WAIT_ABANDONED:
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
		default:
			trx_error("[file:'%s',line:%d] lock failed: %s",
				filename, line, strerror_from_system(GetLastError()));
			exit(EXIT_FAILURE);
	}
#else
#ifdef	HAVE_PTHREAD_PROCESS_SHARED
	if (0 != locks_disabled)
		return;

	if (0 != pthread_mutex_lock(mutex))
	{
		trx_error("[file:'%s',line:%d] lock failed: %s", filename, line, trx_strerror(errno));
		exit(EXIT_FAILURE);
	}
#else
	sem_lock.sem_num = mutex;
	sem_lock.sem_op = -1;
	sem_lock.sem_flg = SEM_UNDO;

	while (-1 == semop(TRX_SEM_LIST_ID, &sem_lock, 1))
	{
		if (EINTR != errno)
		{
			trx_error("[file:'%s',line:%d] lock failed: %s", filename, line, trx_strerror(errno));
			exit(EXIT_FAILURE);
		}
	}
#endif
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: trx_mutex_unlock                                                 *
 *                                                                            *
 * Purpose: Unlock the mutex                                                  *
 *                                                                            *
 * Parameters: mutex - handle of mutex                                        *
 *                                                                            *
 * Author: Eugene Grigorjev, Alexander Vladishev                              *
 *                                                                            *
 ******************************************************************************/
void	__trx_mutex_unlock(const char *filename, int line, trx_mutex_t mutex)
{
#ifndef _WINDOWS
#ifndef	HAVE_PTHREAD_PROCESS_SHARED
	struct sembuf	sem_unlock;
#endif
#endif

	if (TRX_MUTEX_NULL == mutex)
		return;

#ifdef _WINDOWS
	if (0 == ReleaseMutex(mutex))
	{
		trx_error("[file:'%s',line:%d] unlock failed: %s",
				filename, line, strerror_from_system(GetLastError()));
		exit(EXIT_FAILURE);
	}
#else
#ifdef	HAVE_PTHREAD_PROCESS_SHARED
	if (0 != locks_disabled)
		return;

	if (0 != pthread_mutex_unlock(mutex))
	{
		trx_error("[file:'%s',line:%d] unlock failed: %s", filename, line, trx_strerror(errno));
		exit(EXIT_FAILURE);
	}
#else
	sem_unlock.sem_num = mutex;
	sem_unlock.sem_op = 1;
	sem_unlock.sem_flg = SEM_UNDO;

	while (-1 == semop(TRX_SEM_LIST_ID, &sem_unlock, 1))
	{
		if (EINTR != errno)
		{
			trx_error("[file:'%s',line:%d] unlock failed: %s", filename, line, trx_strerror(errno));
			exit(EXIT_FAILURE);
		}
	}
#endif
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: trx_mutex_destroy                                                *
 *                                                                            *
 * Purpose: Destroy the mutex                                                 *
 *                                                                            *
 * Parameters: mutex - handle of mutex                                        *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
void	trx_mutex_destroy(trx_mutex_t *mutex)
{
#ifdef _WINDOWS
	if (TRX_MUTEX_NULL == *mutex)
		return;

	if (0 == CloseHandle(*mutex))
		trx_error("error on mutex destroying: %s", strerror_from_system(GetLastError()));
#else
#ifdef	HAVE_PTHREAD_PROCESS_SHARED
	if (0 != locks_disabled)
		return;

	if (0 != pthread_mutex_destroy(*mutex))
		trx_error("cannot remove mutex %p: %s", (void *)mutex, trx_strerror(errno));
#else
	if (0 == --mutexes && -1 == semctl(TRX_SEM_LIST_ID, 0, IPC_RMID, 0))
		trx_error("cannot remove semaphore set %d: %s", TRX_SEM_LIST_ID, trx_strerror(errno));
#endif
#endif
	*mutex = TRX_MUTEX_NULL;
}

#ifdef _WINDOWS
/******************************************************************************
 *                                                                            *
 * Function: trx_mutex_create_per_process_name                                *
 *                                                                            *
 * Purpose: Appends PID to the prefix of the mutex                            *
 *                                                                            *
 * Parameters: prefix - mutex type                                            *
 *                                                                            *
 * Return value: Dynamically allocated, NUL terminated name of the mutex      *
 *                                                                            *
 * Comments: The mutex name must be shorter than MAX_PATH characters,         *
 *           otherwise the function calls exit()                              *
 *                                                                            *
 ******************************************************************************/
trx_mutex_name_t	trx_mutex_create_per_process_name(const trx_mutex_name_t prefix)
{
	trx_mutex_name_t	name = TRX_MUTEX_NULL;
	int			size;
	wchar_t			*format = L"%s_PID_%lx";
	DWORD			pid = GetCurrentProcessId();

	/* exit if the mutex name length exceed the maximum allowed */
	size = _scwprintf(format, prefix, pid);
	if (MAX_PATH < size)
	{
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	size = size + 1; /* for terminating '\0' */

	name = trx_malloc(NULL, sizeof(wchar_t) * size);
	(void)_snwprintf_s(name, size, size - 1, format, prefix, pid);
	name[size - 1] = L'\0';

	return name;
}
#endif

