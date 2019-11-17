

#ifndef TREEGIX_DAEMON_H
#define TREEGIX_DAEMON_H

#if defined(_WINDOWS)
#	error "This module allowed only for Unix OS"
#endif

extern char			*CONFIG_PID_FILE;
extern volatile sig_atomic_t	sig_exiting;
#include "threads.h"

int	daemon_start(int allow_root, const char *user, unsigned int flags);
void	daemon_stop(void);

int	zbx_sigusr_send(int flags);
void	zbx_set_sigusr_handler(void (*handler)(int flags));

#define ZBX_IS_RUNNING()	(0 == sig_exiting)
#define ZBX_DO_EXIT()

#define START_MAIN_TREEGIX_ENTRY(allow_root, user, flags)	daemon_start(allow_root, user, flags)

#endif	/* TREEGIX_DAEMON_H */
