

#ifndef TREEGIX_DATASENDER_H
#define TREEGIX_DATASENDER_H

#include "threads.h"

extern int	CONFIG_PROXYDATA_FREQUENCY;

TRX_THREAD_ENTRY(datasender_thread, args);

#endif
