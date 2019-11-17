

#ifndef TREEGIX_HOUSEKEEPER_H
#define TREEGIX_HOUSEKEEPER_H

#include "threads.h"

extern int	CONFIG_HOUSEKEEPING_FREQUENCY;
extern int	CONFIG_PROXY_LOCAL_BUFFER;
extern int	CONFIG_PROXY_OFFLINE_BUFFER;

TRX_THREAD_ENTRY(housekeeper_thread, args);

#endif
