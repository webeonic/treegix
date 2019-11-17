

#ifndef TREEGIX_SIGCOMMON_H
#define TREEGIX_SIGCOMMON_H

extern int	sig_parent_pid;

#define SIG_CHECKED_FIELD(siginfo, field)		(NULL == siginfo ? -1 : (int)siginfo->field)
#define SIG_CHECKED_FIELD_TYPE(siginfo, field, type)	(NULL == siginfo ? (type)-1 : siginfo->field)
#define SIG_PARENT_PROCESS				(sig_parent_pid == (int)getpid())

#define SIG_CHECK_PARAMS(sig, siginfo, context)											\
		if (NULL == siginfo)												\
			treegix_log(LOG_LEVEL_DEBUG, "received [signal:%d(%s)] with NULL siginfo", sig, get_signal_name(sig));	\
		if (NULL == context)												\
			treegix_log(LOG_LEVEL_DEBUG, "received [signal:%d(%s)] with NULL context", sig, get_signal_name(sig))

#endif	/* TREEGIX_SIGCOMMON_H */
