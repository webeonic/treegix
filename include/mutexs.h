
#ifndef TREEGIX_MUTEXS_H
#define TREEGIX_MUTEXS_H

#ifdef _WINDOWS
#	define TRX_MUTEX_NULL		NULL

#	define TRX_MUTEX_LOG		zbx_mutex_create_per_process_name(L"TRX_MUTEX_LOG")
#	define TRX_MUTEX_PERFSTAT	zbx_mutex_create_per_process_name(L"TRX_MUTEX_PERFSTAT")

typedef wchar_t * zbx_mutex_name_t;
typedef HANDLE zbx_mutex_t;
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
zbx_mutex_name_t;

typedef enum
{
	TRX_RWLOCK_CONFIG = 0,
	TRX_RWLOCK_COUNT,
}
zbx_rwlock_name_t;

#ifdef HAVE_PTHREAD_PROCESS_SHARED
#	define TRX_MUTEX_NULL			NULL
#	define TRX_RWLOCK_NULL			NULL

#	define zbx_rwlock_wrlock(rwlock)	__zbx_rwlock_wrlock(__FILE__, __LINE__, rwlock)
#	define zbx_rwlock_rdlock(rwlock)	__zbx_rwlock_rdlock(__FILE__, __LINE__, rwlock)
#	define zbx_rwlock_unlock(rwlock)	__zbx_rwlock_unlock(__FILE__, __LINE__, rwlock)

typedef pthread_mutex_t * zbx_mutex_t;
typedef pthread_rwlock_t * zbx_rwlock_t;

void	__zbx_rwlock_wrlock(const char *filename, int line, zbx_rwlock_t rwlock);
void	__zbx_rwlock_rdlock(const char *filename, int line, zbx_rwlock_t rwlock);
void	__zbx_rwlock_unlock(const char *filename, int line, zbx_rwlock_t rwlock);
void	zbx_rwlock_destroy(zbx_rwlock_t *rwlock);
void	zbx_locks_disable(void);
#else	/* fallback to semaphores if read-write locks are not available */
#	define TRX_RWLOCK_NULL				-1
#	define TRX_MUTEX_NULL				-1

#	define zbx_rwlock_wrlock(rwlock)		__zbx_mutex_lock(__FILE__, __LINE__, rwlock)
#	define zbx_rwlock_rdlock(rwlock)		__zbx_mutex_lock(__FILE__, __LINE__, rwlock)
#	define zbx_rwlock_unlock(rwlock)		__zbx_mutex_unlock(__FILE__, __LINE__, rwlock)
#	define zbx_rwlock_destroy(rwlock)		zbx_mutex_destroy(rwlock)

typedef int zbx_mutex_t;
typedef int zbx_rwlock_t;
#endif
int	zbx_locks_create(char **error);
int	zbx_rwlock_create(zbx_rwlock_t *rwlock, zbx_rwlock_name_t name, char **error);
#endif	/* _WINDOWS */
#	define zbx_mutex_lock(mutex)		__zbx_mutex_lock(__FILE__, __LINE__, mutex)
#	define zbx_mutex_unlock(mutex)		__zbx_mutex_unlock(__FILE__, __LINE__, mutex)

int	zbx_mutex_create(zbx_mutex_t *mutex, zbx_mutex_name_t name, char **error);
void	__zbx_mutex_lock(const char *filename, int line, zbx_mutex_t mutex);
void	__zbx_mutex_unlock(const char *filename, int line, zbx_mutex_t mutex);
void	zbx_mutex_destroy(zbx_mutex_t *mutex);

#ifdef _WINDOWS
zbx_mutex_name_t	zbx_mutex_create_per_process_name(const zbx_mutex_name_t prefix);
#endif

#endif	/* TREEGIX_MUTEXS_H */
