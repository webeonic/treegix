
#ifndef TREEGIX_MUTEXS_H
#define TREEGIX_MUTEXS_H

#ifdef _WINDOWS
#	define TRX_MUTEX_NULL		NULL

#	define TRX_MUTEX_LOG		trx_mutex_create_per_process_name(L"TRX_MUTEX_LOG")
#	define TRX_MUTEX_PERFSTAT	trx_mutex_create_per_process_name(L"TRX_MUTEX_PERFSTAT")

typedef wchar_t * trx_mutex_name_t;
typedef HANDLE trx_mutex_t;
#else	/* not _WINDOWS */
typedef enum
{
	TRX_MUTEX_LOG = 0,
	TRX_MUTEX_CACHE,
	TRX_MUTEX_TRENDS,
	TRX_MUTEX_CACHE_IDS,
	TRX_MUTEX_SELFMON,
	TRX_MUTEX_CPUSTATS,
	TRX_MUTEX_DISKSTATS,
	TRX_MUTEX_ITSERVICES,
	TRX_MUTEX_VALUECACHE,
	TRX_MUTEX_VMWARE,
	TRX_MUTEX_SQLITE3,
	TRX_MUTEX_PROCSTAT,
	TRX_MUTEX_PROXY_HISTORY,
	TRX_MUTEX_COUNT
}
trx_mutex_name_t;

typedef enum
{
	TRX_RWLOCK_CONFIG = 0,
	TRX_RWLOCK_COUNT,
}
trx_rwlock_name_t;

#ifdef HAVE_PTHREAD_PROCESS_SHARED
#	define TRX_MUTEX_NULL			NULL
#	define TRX_RWLOCK_NULL			NULL

#	define trx_rwlock_wrlock(rwlock)	__trx_rwlock_wrlock(__FILE__, __LINE__, rwlock)
#	define trx_rwlock_rdlock(rwlock)	__trx_rwlock_rdlock(__FILE__, __LINE__, rwlock)
#	define trx_rwlock_unlock(rwlock)	__trx_rwlock_unlock(__FILE__, __LINE__, rwlock)

typedef pthread_mutex_t * trx_mutex_t;
typedef pthread_rwlock_t * trx_rwlock_t;

void	__trx_rwlock_wrlock(const char *filename, int line, trx_rwlock_t rwlock);
void	__trx_rwlock_rdlock(const char *filename, int line, trx_rwlock_t rwlock);
void	__trx_rwlock_unlock(const char *filename, int line, trx_rwlock_t rwlock);
void	trx_rwlock_destroy(trx_rwlock_t *rwlock);
void	trx_locks_disable(void);
#else	/* fallback to semaphores if read-write locks are not available */
#	define TRX_RWLOCK_NULL				-1
#	define TRX_MUTEX_NULL				-1

#	define trx_rwlock_wrlock(rwlock)		__trx_mutex_lock(__FILE__, __LINE__, rwlock)
#	define trx_rwlock_rdlock(rwlock)		__trx_mutex_lock(__FILE__, __LINE__, rwlock)
#	define trx_rwlock_unlock(rwlock)		__trx_mutex_unlock(__FILE__, __LINE__, rwlock)
#	define trx_rwlock_destroy(rwlock)		trx_mutex_destroy(rwlock)

typedef int trx_mutex_t;
typedef int trx_rwlock_t;
#endif
int	trx_locks_create(char **error);
int	trx_rwlock_create(trx_rwlock_t *rwlock, trx_rwlock_name_t name, char **error);
#endif	/* _WINDOWS */
#	define trx_mutex_lock(mutex)		__trx_mutex_lock(__FILE__, __LINE__, mutex)
#	define trx_mutex_unlock(mutex)		__trx_mutex_unlock(__FILE__, __LINE__, mutex)

int	trx_mutex_create(trx_mutex_t *mutex, trx_mutex_name_t name, char **error);
void	__trx_mutex_lock(const char *filename, int line, trx_mutex_t mutex);
void	__trx_mutex_unlock(const char *filename, int line, trx_mutex_t mutex);
void	trx_mutex_destroy(trx_mutex_t *mutex);

#ifdef _WINDOWS
trx_mutex_name_t	trx_mutex_create_per_process_name(const trx_mutex_name_t prefix);
#endif

#endif	/* TREEGIX_MUTEXS_H */
