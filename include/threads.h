
#ifndef TREEGIX_THREADS_H
#define TREEGIX_THREADS_H

#include "common.h"

#if defined(_WINDOWS)
	/* the TRXEndThread function is implemented in service.c file */
	void	CALLBACK TRXEndThread(ULONG_PTR dwParam);

	#define TRX_THREAD_ERROR	0

	#define TRX_THREAD_HANDLE	HANDLE
	#define TRX_THREAD_HANDLE_NULL	NULL

	#define TRX_THREAD_WAIT_EXIT	1

	#define TRX_THREAD_ENTRY_POINTER(pointer_name) \
		unsigned (__stdcall *pointer_name)(void *)

	#define TRX_THREAD_ENTRY(entry_name, arg_name)	\
		unsigned __stdcall entry_name(void *arg_name)

	#define zbx_thread_exit(status) \
		_endthreadex((unsigned int)(status)); \
		return ((unsigned)(status))

	#define zbx_sleep(sec) SleepEx(((DWORD)(sec)) * ((DWORD)1000), TRUE)

	#define zbx_thread_kill(h) QueueUserAPC(TRXEndThread, h, 0)
	#define zbx_thread_kill_fatal(h) QueueUserAPC(TRXEndThread, h, 0)
#else	/* not _WINDOWS */

	int	zbx_fork(void);
	void	zbx_child_fork(pid_t *pid);

	#define TRX_THREAD_ERROR	-1

	#define TRX_THREAD_HANDLE	pid_t
	#define TRX_THREAD_HANDLE_NULL	0

	#define TRX_THREAD_WAIT_EXIT	1

	#define TRX_THREAD_ENTRY_POINTER(pointer_name) \
		unsigned (* pointer_name)(void *)

	#define TRX_THREAD_ENTRY(entry_name, arg_name)	\
		unsigned entry_name(void *arg_name)

	/* Calling _exit() to terminate child process immediately is important. See TRX-5732 for details. */
	#define zbx_thread_exit(status) \
		_exit((int)(status)); \
		return ((unsigned)(status))

	#define zbx_sleep(sec) sleep((sec))

	#define zbx_thread_kill(h) kill(h, SIGUSR2)
	#define zbx_thread_kill_fatal(h) kill(h, SIGHUP)
#endif	/* _WINDOWS */

typedef struct
{
	int		server_num;
	int		process_num;
	unsigned char	process_type;
	void		*args;
#ifdef _WINDOWS
	TRX_THREAD_ENTRY_POINTER(entry);
#endif
}
zbx_thread_args_t;

void	zbx_thread_start(TRX_THREAD_ENTRY_POINTER(handler), zbx_thread_args_t *thread_args, TRX_THREAD_HANDLE *thread);
int	zbx_thread_wait(TRX_THREAD_HANDLE thread);
void			zbx_threads_wait(TRX_THREAD_HANDLE *threads, const int *threads_flags, int threads_num, int ret);
/* zbx_thread_exit(status) -- declared as define !!! */
long int		zbx_get_thread_id(void);

#endif	/* TREEGIX_THREADS_H */
