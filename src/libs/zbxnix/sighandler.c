

#include "common.h"
#include "sighandler.h"

#include "log.h"
#include "fatal.h"
#include "sigcommon.h"
#include "../../libs/trxcrypto/tls.h"

int			sig_parent_pid = -1;
volatile sig_atomic_t	sig_exiting;

static void	log_fatal_signal(int sig, siginfo_t *siginfo, void *context)
{
	SIG_CHECK_PARAMS(sig, siginfo, context);

	treegix_log(LOG_LEVEL_CRIT, "Got signal [signal:%d(%s),reason:%d,refaddr:%p]. Crashing ...",
			sig, get_signal_name(sig),
			SIG_CHECKED_FIELD(siginfo, si_code),
			SIG_CHECKED_FIELD_TYPE(siginfo, si_addr, void *));
}

static void	exit_with_failure(void)
{
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	trx_tls_free_on_signal();
#endif
	_exit(EXIT_FAILURE);
}

/******************************************************************************
 *                                                                            *
 * Function: fatal_signal_handler                                             *
 *                                                                            *
 * Purpose: handle fatal signals: SIGILL, SIGFPE, SIGSEGV, SIGBUS             *
 *                                                                            *
 ******************************************************************************/
static void	fatal_signal_handler(int sig, siginfo_t *siginfo, void *context)
{
	log_fatal_signal(sig, siginfo, context);
	trx_log_fatal_info(context, TRX_FATAL_LOG_FULL_INFO);

	exit_with_failure();
}

/******************************************************************************
 *                                                                            *
 * Function: metric_thread_signal_handler                                     *
 *                                                                            *
 * Purpose: same as fatal_signal_handler() but customized for metric thread - *
 *          does not log memory map                                           *
 *                                                                            *
 ******************************************************************************/
static void	metric_thread_signal_handler(int sig, siginfo_t *siginfo, void *context)
{
	log_fatal_signal(sig, siginfo, context);
	trx_log_fatal_info(context, (TRX_FATAL_LOG_PC_REG_SF | TRX_FATAL_LOG_BACKTRACE));

	exit_with_failure();
}

/******************************************************************************
 *                                                                            *
 * Function: alarm_signal_handler                                             *
 *                                                                            *
 * Purpose: handle alarm signal SIGALRM                                       *
 *                                                                            *
 ******************************************************************************/
static void	alarm_signal_handler(int sig, siginfo_t *siginfo, void *context)
{
	SIG_CHECK_PARAMS(sig, siginfo, context);

	trx_alarm_flag_set();	/* set alarm flag */
}

/******************************************************************************
 *                                                                            *
 * Function: terminate_signal_handler                                         *
 *                                                                            *
 * Purpose: handle terminate signals: SIGHUP, SIGINT, SIGTERM, SIGUSR2        *
 *                                                                            *
 ******************************************************************************/
static void	terminate_signal_handler(int sig, siginfo_t *siginfo, void *context)
{
	if (!SIG_PARENT_PROCESS)
	{
		/* the parent process can either politely ask a child process to finish it's work and perform cleanup */
		/* by sending SIGUSR2 or terminate child process immediately without cleanup by sending SIGHUP        */
		if (SIGHUP == sig)
			exit_with_failure();

		if (SIGUSR2 == sig)
			sig_exiting = 1;
	}
	else
	{
		SIG_CHECK_PARAMS(sig, siginfo, context);

		if (0 == sig_exiting)
		{
			sig_exiting = 1;
			treegix_log(sig_parent_pid == SIG_CHECKED_FIELD(siginfo, si_pid) ?
					LOG_LEVEL_DEBUG : LOG_LEVEL_WARNING,
					"Got signal [signal:%d(%s),sender_pid:%d,sender_uid:%d,"
					"reason:%d]. Exiting ...",
					sig, get_signal_name(sig),
					SIG_CHECKED_FIELD(siginfo, si_pid),
					SIG_CHECKED_FIELD(siginfo, si_uid),
					SIG_CHECKED_FIELD(siginfo, si_code));

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
			trx_tls_free_on_signal();
#endif
			trx_on_exit(SUCCEED);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: child_signal_handler                                             *
 *                                                                            *
 * Purpose: handle child signal SIGCHLD                                       *
 *                                                                            *
 ******************************************************************************/
static void	child_signal_handler(int sig, siginfo_t *siginfo, void *context)
{
	SIG_CHECK_PARAMS(sig, siginfo, context);

	if (!SIG_PARENT_PROCESS)
		exit_with_failure();

	if (0 == sig_exiting)
	{
		sig_exiting = 1;
		treegix_log(LOG_LEVEL_CRIT, "One child process died (PID:%d,exitcode/signal:%d). Exiting ...",
				SIG_CHECKED_FIELD(siginfo, si_pid), SIG_CHECKED_FIELD(siginfo, si_status));

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		trx_tls_free_on_signal();
#endif
		trx_on_exit(FAIL);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: trx_set_common_signal_handlers                                   *
 *                                                                            *
 * Purpose: set the commonly used signal handlers                             *
 *                                                                            *
 ******************************************************************************/
void	trx_set_common_signal_handlers(void)
{
	struct sigaction	phan;

	sig_parent_pid = (int)getpid();

	sigemptyset(&phan.sa_mask);
	phan.sa_flags = SA_SIGINFO;

	phan.sa_sigaction = terminate_signal_handler;
	sigaction(SIGINT, &phan, NULL);
	sigaction(SIGQUIT, &phan, NULL);
	sigaction(SIGHUP, &phan, NULL);
	sigaction(SIGTERM, &phan, NULL);
	sigaction(SIGUSR2, &phan, NULL);

	phan.sa_sigaction = fatal_signal_handler;
	sigaction(SIGILL, &phan, NULL);
	sigaction(SIGFPE, &phan, NULL);
	sigaction(SIGSEGV, &phan, NULL);
	sigaction(SIGBUS, &phan, NULL);

	phan.sa_sigaction = alarm_signal_handler;
	sigaction(SIGALRM, &phan, NULL);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_set_child_signal_handler                                     *
 *                                                                            *
 * Purpose: set the handlers for child process signals                        *
 *                                                                            *
 ******************************************************************************/
void 	trx_set_child_signal_handler(void)
{
	struct sigaction	phan;

	sig_parent_pid = (int)getpid();

	sigemptyset(&phan.sa_mask);
	phan.sa_flags = SA_SIGINFO | SA_NOCLDSTOP;

	phan.sa_sigaction = child_signal_handler;
	sigaction(SIGCHLD, &phan, NULL);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_set_metric_thread_signal_handler                             *
 *                                                                            *
 * Purpose: set the handlers for child process signals                        *
 *                                                                            *
 ******************************************************************************/
void 	trx_set_metric_thread_signal_handler(void)
{
	struct sigaction	phan;

	sig_parent_pid = (int)getpid();

	sigemptyset(&phan.sa_mask);
	phan.sa_flags = SA_SIGINFO;

	phan.sa_sigaction = metric_thread_signal_handler;
	sigaction(SIGILL, &phan, NULL);
	sigaction(SIGFPE, &phan, NULL);
	sigaction(SIGSEGV, &phan, NULL);
	sigaction(SIGBUS, &phan, NULL);
}
