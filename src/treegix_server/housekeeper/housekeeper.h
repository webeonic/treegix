

#ifndef TREEGIX_HOUSEKEEPER_H
#define TREEGIX_HOUSEKEEPER_H

#include "threads.h"

extern int	CONFIG_HOUSEKEEPING_FREQUENCY;
extern int	CONFIG_MAX_HOUSEKEEPER_DELETE;

TRX_THREAD_ENTRY(housekeeper_thread, args);

#endif
