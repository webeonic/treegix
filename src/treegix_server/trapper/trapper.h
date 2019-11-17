

#ifndef TREEGIX_TRAPPER_H
#define TREEGIX_TRAPPER_H

#include "comms.h"
#include "threads.h"

extern int	CONFIG_TIMEOUT;
extern int	CONFIG_TRAPPER_TIMEOUT;
extern char	*CONFIG_STATS_ALLOWED_IP;

TRX_THREAD_ENTRY(trapper_thread, args);

#endif
