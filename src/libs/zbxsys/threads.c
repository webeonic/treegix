

#include "common.h"
#include "log.h"
#include "threads.h"

#if !defined(_WINDOWS)
/******************************************************************************
 *                                                                            *
 * Function: trx_fork                                                         *
 *                                                                            *
 * Purpose: Flush stdout and stderr before forking                            *
 *                                                                            *
 * Return value: same as system fork() function                               *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
int	trx_fork(void)
{
	fflush(stdout);
	fflush(stderr);
	return fork();
}

/******************************************************************************
 *                                                                            *
 * Function: trx_child_fork                                                   *
 *                                                                            *
 * Purpose: fork from master process and set SIGCHLD handler                  *
 *                                                                            *
 * Return value: same as system fork() function                               *
 *                                                                            *
 * Author: Rudolfs Kreicbergs                                                 *
 *                                                                            *
 * Comments: use this function only for forks from the main process           *
 *                                                                            *
 ******************************************************************************/
void	trx_child_fork(pid_t *pid)
{
	sigset_t	mask, orig_mask;

	/* block signals during fork to avoid deadlock (we've seen one in __unregister_atfork()) */
	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGUSR2);
	sigaddset(&mask, SIGHUP);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGQUIT);
	sigaddset(&mask, SIGCHLD);

	sigprocmask(SIG_BLOCK, &mask, &orig_mask);

	/* set process id instead of returning, this is to avoid race condition when signal arrives before return */
	*pid = trx_fork();

	sigprocmask(SIG_SETMASK, &orig_mask, NULL);

	/* ignore SIGCHLD to avoid problems with exiting scripts in trx_execute() and other cases */
	if (0 == *pid)
		signal(SIGCHLD, SIG_DFL);
}
#else
int	trx_win_exception_filter(unsigned int code, struct _EXCEPTION_POINTERS *ep);

static TRX_THREAD_ENTRY(trx_win_thread_entry, args)
{
	__try
	{
		trx_thread_args_t	*thread_args = (trx_thread_args_t *)args;

		return thread_args->entry(thread_args);
	}
	__except(trx_win_exception_filter(GetExceptionCode(), GetExceptionInformation()))
	{
		trx_thread_exit(EXIT_SUCCESS);
	}
}

void CALLBACK	TRXEndThread(ULONG_PTR dwParam)
{
	_endthreadex(SUCCEED);
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: trx_thread_start                                                 *
 *                                                                            *
 * Purpose: Start the handled function as "thread"                            *
 *                                                                            *
 * Parameters: handler     - [IN] new thread starts execution from this       *
 *                                handler function                            *
 *             thread_args - [IN] arguments for thread function               *
 *             thread      - [OUT] handle to a newly created thread           *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments: The trx_thread_exit must be called from the handler!             *
 *                                                                            *
 ******************************************************************************/
void	trx_thread_start(TRX_THREAD_ENTRY_POINTER(handler), trx_thread_args_t *thread_args, TRX_THREAD_HANDLE *thread)
{
#ifdef _WINDOWS
	unsigned		thrdaddr;

	thread_args->entry = handler;
	/* NOTE: _beginthreadex returns 0 on failure, rather than 1 */
	if (0 == (*thread = (TRX_THREAD_HANDLE)_beginthreadex(NULL, 0, trx_win_thread_entry, thread_args, 0, &thrdaddr)))
	{
		treegix_log(LOG_LEVEL_CRIT, "failed to create a thread: %s", strerror_from_system(GetLastError()));
		*thread = (TRX_THREAD_HANDLE)TRX_THREAD_ERROR;
	}
#else
	trx_child_fork(thread);

	if (0 == *thread)	/* child process */
	{
		(*handler)(thread_args);

		/* The trx_thread_exit must be called from the handler. */
		/* And in normal case the program will never reach this point. */
		THIS_SHOULD_NEVER_HAPPEN;
		/* program will never reach this point */
	}
	else if (-1 == *thread)
	{
		trx_error("failed to fork: %s", trx_strerror(errno));
		*thread = (TRX_THREAD_HANDLE)TRX_THREAD_ERROR;
	}
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: trx_thread_wait                                                  *
 *                                                                            *
 * Purpose: Waits until the "thread" is in the signalled state                *
 *                                                                            *
 * Parameters: "thread" handle                                                *
 *                                                                            *
 * Return value: process or thread exit code                                  *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
int	trx_thread_wait(TRX_THREAD_HANDLE thread)
{
	int	status = 0;	/* significant 8 bits of the status */

#ifdef _WINDOWS

	if (WAIT_OBJECT_0 != WaitForSingleObject(thread, INFINITE))
	{
		trx_error("Error on thread waiting. [%s]", strerror_from_system(GetLastError()));
		return TRX_THREAD_ERROR;
	}

	if (0 == GetExitCodeThread(thread, &status))
	{
		trx_error("Error on thread exit code receiving. [%s]", strerror_from_system(GetLastError()));
		return TRX_THREAD_ERROR;
	}

	if (0 == CloseHandle(thread))
	{
		trx_error("Error on thread closing. [%s]", strerror_from_system(GetLastError()));
		return TRX_THREAD_ERROR;
	}

#else	/* not _WINDOWS */

	if (0 >= waitpid(thread, &status, 0))
	{
		trx_error("Error waiting for process with PID %d: %s", (int)thread, trx_strerror(errno));
		return TRX_THREAD_ERROR;
	}

	status = WEXITSTATUS(status);

#endif	/* _WINDOWS */

	return status;
}

/******************************************************************************
 *                                                                            *
 * Function: threads_kill                                                     *
 *                                                                            *
 * Purpose: sends termination signal to "threads"                             *
 *                                                                            *
 * Parameters: threads     - [IN] handles to threads or processes             *
 *             threads_num - [IN] number of handles                           *
 *             ret         - [IN] terminate thread politely on SUCCEED or ask *
 *                                threads to exit immediately on FAIL         *
 *                                                                            *
 ******************************************************************************/
static void	threads_kill(TRX_THREAD_HANDLE *threads, int threads_num, int ret)
{
	int	i;

	for (i = 0; i < threads_num; i++)
	{
		if (!threads[i])
			continue;

		if (SUCCEED != ret)
			trx_thread_kill_fatal(threads[i]);
		else
			trx_thread_kill(threads[i]);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: trx_threads_wait                                                 *
 *                                                                            *
 * Purpose: Waits until the "threads" are in the signalled state              *
 *                                                                            *
 * Parameters: "threads" handles                                              *
 *                                                                            *
 *                                                                            *
 ******************************************************************************/
void	trx_threads_wait(TRX_THREAD_HANDLE *threads, const int *threads_flags, int threads_num, int ret)
{
	int		i;
#if !defined(_WINDOWS)
	sigset_t	set;

	/* ignore SIGCHLD signals in order for trx_sleep() to work */
	sigemptyset(&set);
	sigaddset(&set, SIGCHLD);
	sigprocmask(SIG_BLOCK, &set, NULL);

	/* signal all threads to go into idle state and wait for flagged threads to exit */
	threads_kill(threads, threads_num, ret);

	for (i = 0; i < threads_num; i++)
	{
		if (!threads[i] || TRX_THREAD_WAIT_EXIT != threads_flags[i])
			continue;

		trx_thread_wait(threads[i]);

		threads[i] = TRX_THREAD_HANDLE_NULL;
	}

	/* signal idle threads to exit */
	threads_kill(threads, threads_num, FAIL);
#else
	/* wait for threads to finish first. although listener threads will never end */
	WaitForMultipleObjectsEx(threads_num, threads, TRUE, 1000, FALSE);
	threads_kill(threads, threads_num, ret);
#endif

	for (i = 0; i < threads_num; i++)
	{
		if (!threads[i])
			continue;

		trx_thread_wait(threads[i]);

		threads[i] = TRX_THREAD_HANDLE_NULL;
	}
}

long int	trx_get_thread_id(void)
{
#ifdef _WINDOWS
	return (long int)GetCurrentThreadId();
#else
	return (long int)getpid();
#endif
}
