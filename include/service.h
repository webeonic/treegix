

#ifndef TREEGIX_SERVICE_H
#define TREEGIX_SERVICE_H

#ifndef _WINDOWS
#	error "This module is only available for Windows OS"
#endif

#include "threads.h"

extern ZBX_THREAD_HANDLE	*threads;

void	service_start(int flags);

int	TreegixCreateService(const char *path, int multiple_agents);
int	TreegixRemoveService(void);
int	TreegixStartService(void);
int	TreegixStopService(void);

void	set_parent_signal_handler(void);

int	application_status;	/* required for closing application from service */

#define ZBX_APP_STOPPED	0
#define ZBX_APP_RUNNING	1

#define ZBX_IS_RUNNING()	(ZBX_APP_RUNNING == application_status)
#define ZBX_DO_EXIT()		application_status = ZBX_APP_STOPPED

#define START_MAIN_TREEGIX_ENTRY(allow_root, user, flags)	service_start(flags)

#endif /* TREEGIX_SERVICE_H */
