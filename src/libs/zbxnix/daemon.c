

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

static void	(*zbx_sigusr_handler)(int flags);

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
	switch (ZBX_RTC_GET_MSG(flags))
	{
		case ZBX_RTC_LOG_LEVEL_INCREASE:
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
		case ZBX_RTC_LOG_LEVEL_DECREASE:
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
			if (NULL != zbx_sigusr_handler)
				zbx_sigusr_handler(flags);
			break;
	}
}

static void	zbx_signal_process_by_type(int proc_type, int proc_num, int flags)
{
	int		process_num, found = 0, i;
	union sigval	s;
	unsigned char	process_type;

	s.ZBX_SIVAL_INT = flags;

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
			treegix_log(LOG_LEVEL_ERR, "cannot redirect signal: %s", zbx_strerror(errno));
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

static void	zbx_signal_process_by_pid(int pid, int flags)
{
	union sigval	s;
	int		i, found = 0;

	s.ZBX_SIVAL_INT = flags;

	for (i = 0; i < threads_num; i++)
	{
		if (0 != pid && threads[i] != ZBX_RTC_GET_DATA(flags))
			continue;

		found = 1;

		if (-1 != sigqueue(threads[i], SIGUSR1, s))
		{
			treegix_log(LOG_LEVEL_DEBUG, "the signal was redirected to process pid:%d",
					threads[i]);
		}
		else
			treegix_log(LOG_LEVEL_ERR, "cannot redirect signal: %s", zbx_strerror(errno));
	}

	if (0 != ZBX_RTC_GET_DATA(flags) && 0 == found)
	{
		treegix_log(LOG_LEVEL_ERR, "cannot redirect signal: process pid:%d is not a Treegix child"
				" process", ZBX_RTC_GET_DATA(flags));
	}
}

#endif

void	zbx_set_sigusr_handler(void (*handler)(int flags))
{
	zbx_sigusr_handler = handler;
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
			SIG_CHECKED_FIELD(siginfo, si_value.ZBX_SIVAL_INT),
			(unsigned int)SIG_CHECKED_FIELD(siginfo, si_value.ZBX_SIVAL_INT));
#ifdef HAVE_SIGQUEUE
	flags = SIG_CHECKED_FIELD(siginfo, si_value.ZBX_SIVAL_INT);

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

	switch (ZBX_RTC_GET_MSG(flags))
	{
		case ZBX_RTC_CONFIG_CACHE_RELOAD:
			if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY_PASSIVE))
			{
				treegix_log(LOG_LEVEL_WARNING, "forced reloading of the configuration cache"
						" cannot be performed for a passive proxy");
				return;
			}

			zbx_signal_process_by_type(ZBX_PROCESS_TYPE_CONFSYNCER, 1, flags);
			break;
		case ZBX_RTC_HOUSEKEEPER_EXECUTE:
			zbx_signal_process_by_type(ZBX_PROCESS_TYPE_HOUSEKEEPER, 1, flags);
			break;
		case ZBX_RTC_LOG_LEVEL_INCREASE:
		case ZBX_RTC_LOG_LEVEL_DECREASE:
			if ((ZBX_RTC_LOG_SCOPE_FLAG | ZBX_RTC_LOG_SCOPE_PID) == ZBX_RTC_GET_SCOPE(flags))
				zbx_signal_process_by_pid(ZBX_RTC_GET_DATA(flags), flags);
			else
				zbx_signal_process_by_type(ZBX_RTC_GET_SCOPE(flags), ZBX_RTC_GET_DATA(flags), flags);
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
			zbx_error("user %s does not exist", user);
			zbx_error("cannot run as root!");
			exit(EXIT_FAILURE);
		}

		if (0 == pwd->pw_uid)
		{
			zbx_error("User=%s contradicts AllowRoot=0", user);
			zbx_error("cannot run as root!");
			exit(EXIT_FAILURE);
		}

		if (-1 == setgid(pwd->pw_gid))
		{
			zbx_error("cannot setgid to %s: %s", user, zbx_strerror(errno));
			exit(EXIT_FAILURE);
		}

#ifdef HAVE_FUNCTION_INITGROUPS
		if (-1 == initgroups(user, pwd->pw_gid))
		{
			zbx_error("cannot initgroups to %s: %s", user, zbx_strerror(errno));
			exit(EXIT_FAILURE);
		}
#endif

		if (-1 == setuid(pwd->pw_uid))
		{
			zbx_error("cannot setuid to %s: %s", user, zbx_strerror(errno));
			exit(EXIT_FAILURE);
		}

#ifdef HAVE_FUNCTION_SETEUID
		if (-1 == setegid(pwd->pw_gid) || -1 == seteuid(pwd->pw_uid))
		{
			zbx_error("cannot setegid or seteuid to %s: %s", user, zbx_strerror(errno));
			exit(EXIT_FAILURE);
		}
#endif
	}

	umask(0002);

	if (0 == (flags & ZBX_TASK_FLAG_FOREGROUND))
	{
		if (0 != (pid = zbx_fork()))
			exit(EXIT_SUCCESS);

		setsid();

		signal(SIGHUP, SIG_IGN);

		if (0 != (pid = zbx_fork()))
			exit(EXIT_SUCCESS);

		if (-1 == chdir("/"))	/* this is to eliminate warning: ignoring return value of chdir */
			assert(0);

		if (FAIL == zbx_redirect_stdio(LOG_TYPE_FILE == CONFIG_LOG_TYPE ? CONFIG_LOG_FILE : NULL))
			exit(EXIT_FAILURE);
	}

	if (FAIL == create_pid_file(CONFIG_PID_FILE))
		exit(EXIT_FAILURE);

	atexit(daemon_stop);

	parent_pid = (int)getpid();

	zbx_set_common_signal_handlers();
	set_daemon_signal_handlers();

	/* Set SIGCHLD now to avoid race conditions when a child process is created before */
	/* sigaction() is called. To avoid problems when scripts exit in zbx_execute() and */
	/* other cases, SIGCHLD is set to SIG_DFL in zbx_child_fork(). */
	zbx_set_child_signal_handler();

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

int	zbx_sigusr_send(int flags)
{
	int	ret = FAIL;
	char	error[256];
#ifdef HAVE_SIGQUEUE
	pid_t	pid;

	if (SUCCEED == read_pid_file(CONFIG_PID_FILE, &pid, error, sizeof(error)))
	{
		union sigval	s;

		s.ZBX_SIVAL_INT = flags;

		if (-1 != sigqueue(pid, SIGUSR1, s))
		{
			zbx_error("command sent successfully");
			ret = SUCCEED;
		}
		else
		{
			zbx_snprintf(error, sizeof(error), "cannot send command to PID [%d]: %s",
					(int)pid, zbx_strerror(errno));
		}
	}
#else
	zbx_snprintf(error, sizeof(error), "operation is not supported on the given operating system");
#endif
	if (SUCCEED != ret)
		zbx_error("%s", error);

	return ret;
}
