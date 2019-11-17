

#ifndef TREEGIX_POLLER_H
#define TREEGIX_POLLER_H

#include "threads.h"

extern int	CONFIG_TIMEOUT;
extern int	CONFIG_UNAVAILABLE_DELAY;
extern int	CONFIG_UNREACHABLE_PERIOD;
extern int	CONFIG_UNREACHABLE_DELAY;

TRX_THREAD_ENTRY(poller_thread, args);

void	zbx_activate_item_host(DC_ITEM *item, zbx_timespec_t *ts);
void	zbx_deactivate_item_host(DC_ITEM *item, zbx_timespec_t *ts, const char *error);

#endif
