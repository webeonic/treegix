

#include "common.h"
#include "daemon.h"

#include "pid.h"
#include "cfg.h"
#include "log.h"
#include "control.h"

#include "fatal.h"
#include "sighandler.h"
#include "sigcommon.h"

char		*CONFIG_PID_FILE = NULL;
static int	parent_pid = -1;

extern pid_t	*threads;
extern int	threads_num;

#ifdef HAVE_SIGQUEUE
extern unsigned char	program_type;
#endif

extern int	get_process_info_by_thread(int local_server_num, unsigned char *local_process_type,
		int *local_process_num);

static void	(*trx_sigusr_handler)(int flags);

#ifdef HAVE_SIGQUEUE
/******************************************************************************
 *                                                                            *
 * Function: common_sigusr_handler                                            *
 *                                                                            *
 * Purpose: common SIGUSR1 handler for Treegix processes                       *
 *                                                                            *
 ******************************************************************************/
static void	common_sigusr_handler(int flags)
{
	switch (TRX_RTC_GET_MSG(flags))
	{
		case TRX_RTC_LOG_LEVEL_INCREASE:
			if (SUCCEED != treegix_increase_log_level())
			{
				treegix_log(LOG_LEVEL_INFORMATION, "cannot increase log level:"
						" maximum level has been already set");
			}
			else
			{
				treegix_log(LOG_LEVEL_INFORMATION, "log level has been increased to %s",
						treegix_get_log_level_string());
			}
			break;
		case TRX_RTC_LOG_LEVEL_DECREASE:
			if (SUCCEED != treegix_decrease_log_level())
			{
				treegix_log(LOG_LEVEL_INFORMATION, "cannot decrease log level:"
						" minimum level has been already set");
			}
			else
			{
				treegix_log(LOG_LEVEL_INFORMATION, "log level has been decreased to %s",
						treegix_get_log_level_string());
			}
			break;
		default:
			if (NULL != trx_sigusr_handler)
				trx_sigusr_handler(flags);
			break;
	}
}

static void	trx_signal_process_by_type(int proc_type, int proc_num, int flags)
{
	int		process_num, found = 0, i;
	union sigval	s;
	unsigned char	process_type;

	s.TRX_SIVAL_INT = flags;

	for (i = 0; i < threads_num; i++)
	{
		if (FAIL == get_process_info_by_thread(i + 1, &process_type, &process_num))
			break;

		if (proc_type != process_type)
		{
			/* check if we have already checked processes of target type */
			if (1 == found)
				break;

			continue;
		}

		if (0 != proc_num && proc_num != process_num)
			continue;

		found = 1;

		if (-1 != sigqueue(threads[i], SIGUSR1, s))
		{
			treegix_log(LOG_LEVEL_DEBUG, "the signal was redirected to \"%s\" process"
					" pid:%d", get_process_type_string(process_type), threads[i]);
		}
		else
			treegix_log(LOG_LEVEL_ERR, "cannot redirect signal: %s", trx_strerror(errno));
	}

	if (0 == found)
	{
		if (0 == proc_num)
		{
			treegix_log(LOG_LEVEL_ERR, "cannot redirect signal:"
					" \"%s\" process does not exist",
					get_process_type_string(proc_type));
		}
		else
		{
			treegix_log(LOG_LEVEL_ERR, "cannot redirect signal:"
					" \"%s #%d\" process does not exist",
					get_process_type_string(proc_type), proc_num);
		}
	}
}

static void	trx_signal_process_by_pid(int pid, int flags)
{
	union sigval	s;
	int		i, found = 0;

	s.TRX_SIVAL_INT = flags;

	for (i = 0; i < threads_num; i++)
	{
		if (0 != pid && threads[i] != TRX_RTC_GET_DATA(flags))
			continue;

		found = 1;

		if (-1 != sigqueue(threads[i], SIGUSR1, s))
		{
			treegix_log(LOG_LEVEL_DEBUG, "the signal was redirected to process pid:%d",
					threads[i]);
		}
		else
			treegix_log(LOG_LEVEL_ERR, "cannot redirect signal: %s", trx_strerror(errno));
	}

	if (0 != TRX_RTC_GET_DATA(flags) && 0 == found)
	{
		treegix_log(LOG_LEVEL_ERR, "cannot redirect signal: process pid:%d is not a Treegix child"
				" process", TRX_RTC_GET_DATA(flags));
	}
}

#endif

void	trx_set_sigusr_handler(void (*handler)(int flags))
{
	trx_sigusr_handler = handler;
}

/******************************************************************************
 *                                                                            *
 * Function: user1_signal_handler                                             *
 *                                                                            *
 * Purpose: handle user signal SIGUSR1                                        *
 *                                                                            *
 ******************************************************************************/
static void	user1_signal_handler(int sig, siginfo_t *siginfo, void *context)
{
#ifdef HAVE_SIGQUEUE
	int	flags;
#endif
	SIG_CHECK_PARAMS(sig, siginfo, context);

	treegix_log(LOG_LEVEL_DEBUG, "Got signal [signal:%d(%s),sender_pid:%d,sender_uid:%d,value_int:%d(0x%08x)].",
			sig, get_signal_name(sig),
			SIG_CHECKED_FIELD(siginfo, si_pid),
			SIG_CHECKED_FIELD(siginfo, si_uid),
			SIG_CHECKED_FIELD(siginfo, si_value.TRX_SIVAL_INT),
			(unsigned int)SIG_CHECKED_FIELD(siginfo, si_value.TRX_SIVAL_INT));
#ifdef HAVE_SIGQUEUE
	flags = SIG_CHECKED_FIELD(siginfo, si_value.TRX_SIVAL_INT);

	if (!SIG_PARENT_PROCESS)
	{
		common_sigusr_handler(flags);
		return;
	}

	if (NULL == threads)
	{
		treegix_log(LOG_LEVEL_ERR, "cannot redirect signal: shutdown in progress");
		return;
	}

	switch (TRX_RTC_GET_MSG(flags))
	{
		case TRX_RTC_CONFIG_CACHE_RELOAD:
			if (0 != (program_type & TRX_PROGRAM_TYPE_PROXY_PASSIVE))
			{
				treegix_log(LOG_LEVEL_WARNING, "forced reloading of the configuration cache"
						" cannot be performed for a passive proxy");
				return;
			}

			trx_signal_process_by_type(TRX_PROCESS_TYPE_CONFSYNCER, 1, flags);
			break;
		case TRX_RTC_HOUSEKEEPER_EXECUTE:
			trx_signal_process_by_type(TRX_PROCESS_TYPE_HOUSEKEEPER, 1, flags);
			break;
		case TRX_RTC_LOG_LEVEL_INCREASE:
		case TRX_RTC_LOG_LEVEL_DECREASE:
			if ((TRX_RTC_LOG_SCOPE_FLAG | TRX_RTC_LOG_SCOPE_PID) == TRX_RTC_GET_SCOPE(flags))
				trx_signal_process_by_pid(TRX_RTC_GET_DATA(flags), flags);
			else
				trx_signal_process_by_type(TRX_RTC_GET_SCOPE(flags), TRX_RTC_GET_DATA(flags), flags);
			break;
	}
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: pipe_signal_handler                                              *
 *                                                                            *
 * Purpose: handle pipe signal SIGPIPE                                        *
 *                                                                            *
 ******************************************************************************/
static void	pipe_signal_handler(int sig, siginfo_t *siginfo, void *context)
{
	SIG_CHECK_PARAMS(sig, siginfo, context);

	treegix_log(LOG_LEVEL_DEBUG, "Got signal [signal:%d(%s),sender_pid:%d]. Ignoring ...",
			sig, get_signal_name(sig),
			SIG_CHECKED_FIELD(siginfo, si_pid));
}

/******************************************************************************
 *                                                                            *
 * Function: set_daemon_signal_handlers                                       *
 *                                                                            *
 * Purpose: set the signal handlers used by daemons                           *
 *                                                                            *
 ******************************************************************************/
static void	set_daemon_signal_handlers(void)
{
	struct sigaction	phan;

	sig_parent_pid = (int)getpid();

	sigemptyset(&phan.sa_mask);
	phan.sa_flags = SA_SIGINFO;

	phan.sa_sigaction = user1_signal_handler;
	sigaction(SIGUSR1, &phan, NULL);

	phan.sa_sigaction = pipe_signal_handler;
	sigaction(SIGPIPE, &phan, NULL);
}

/******************************************************************************
 *                                                                            *
 * Function: daemon_start                                                     *
 *                                                                            *
 * Purpose: init process as daemon                                            *
 *                                                                            *
 * Parameters: allow_root - allow root permission for application             *
 *             user       - user on the system to which to drop the           *
 *                          privileges                                        *
 *             flags      - daemon startup flags                              *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: it doesn't allow running under 'root' if allow_root is zero      *
 *                                                                            *
 ******************************************************************************/
int	daemon_start(int allow_root, const char *user, unsigned int flags)
{
	pid_t		pid;
	struct passwd	*pwd;

	if (0 == allow_root && 0 == getuid())	/* running as root? */
	{
		if (NULL == user)
			user = "treegix";

		pwd = getpwnam(user);

		if (NULL == pwd)
		{
			trx_error("user %s does not exist", user);
			trx_error("cannot run as root!");
			exit(EXIT_FAILURE);
		}

		if (0 == pwd->pw_uid)
		{
			trx_error("User=%s contradicts AllowRoot=0", user);
			trx_error("cannot run as root!");
			exit(EXIT_FAILURE);
		}

		if (-1 == setgid(pwd->pw_gid))
		{
			trx_error("cannot setgid to %s: %s", user, trx_strerror(errno));
			exit(EXIT_FAILURE);
		}

#ifdef HAVE_FUNCTION_INITGROUPS
		if (-1 == initgroups(user, pwd->pw_gid))
		{
			trx_error("cannot initgroups to %s: %s", user, trx_strerror(errno));
			exit(EXIT_FAILURE);
		}
#endif

		if (-1 == setuid(pwd->pw_uid))
		{
			trx_error("cannot setuid to %s: %s", user, trx_strerror(errno));
			exit(EXIT_FAILURE);
		}

#ifdef HAVE_FUNCTION_SETEUID
		if (-1 == setegid(pwd->pw_gid) || -1 == seteuid(pwd->pw_uid))
		{
			trx_error("cannot setegid or seteuid to %s: %s", user, trx_strerror(errno));
			exit(EXIT_FAILURE);
		}
#endif
	}

	umask(0002);

	if (0 == (flags & TRX_TASK_FLAG_FOREGROUND))
	{
		if (0 != (pid = trx_fork()))
			exit(EXIT_SUCCESS);

		setsid();

		signal(SIGHUP, SIG_IGN);

		if (0 != (pid = trx_fork()))
			exit(EXIT_SUCCESS);

		if (-1 == chdir("/"))	/* this is to eliminate warning: ignoring return value of chdir */
			assert(0);

		if (FAIL == trx_redirect_stdio(LOG_TYPE_FILE == CONFIG_LOG_TYPE ? CONFIG_LOG_FILE : NULL))
			exit(EXIT_FAILURE);
	}

	if (FAIL == create_pid_file(CONFIG_PID_FILE))
		exit(EXIT_FAILURE);

	atexit(daemon_stop);

	parent_pid = (int)getpid();

	trx_set_common_signal_handlers();
	set_daemon_signal_handlers();

	/* Set SIGCHLD now to avoid race conditions when a child process is created before */
	/* sigaction() is called. To avoid problems when scripts exit in trx_execute() and */
	/* other cases, SIGCHLD is set to SIG_DFL in trx_child_fork(). */
	trx_set_child_signal_handler();

	return MAIN_TREEGIX_ENTRY(flags);
}

void	daemon_stop(void)
{
	/* this function is registered using atexit() to be called when we terminate */
	/* there should be nothing like logging or calls to exit() beyond this point */

	if (parent_pid != (int)getpid())
		return;

	drop_pid_file(CONFIG_PID_FILE);
}

int	trx_sigusr_send(int flags)
{
	int	ret = FAIL;
	char	error[256];
#ifdef HAVE_SIGQUEUE
	pid_t	pid;

	if (SUCCEED == read_pid_file(CONFIG_PID_FILE, &pid, error, sizeof(error)))
	{
		union sigval	s;

		s.TRX_SIVAL_INT = flags;

		if (-1 != sigqueue(pid, SIGUSR1, s))
		{
			trx_error("command sent successfully");
			ret = SUCCEED;
		}
		else
		{
			trx_snprintf(error, sizeof(error), "cannot send command to PID [%d]: %s",
					(int)pid, trx_strerror(errno));
		}
	}
#else
	trx_snprintf(error, sizeof(error), "operation is not supported on the given operating system");
#endif
	if (SUCCEED != ret)
		trx_error("%s", error);

	return ret;
}
