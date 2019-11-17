

#ifndef TREEGIX_PROXYPOLLER_H
#define TREEGIX_PROXYPOLLER_H

#include "threads.h"

extern char	*CONFIG_SOURCE_IP;
extern int	CONFIG_TRAPPER_TIMEOUT;

TRX_THREAD_ENTRY(proxypoller_thread, args);

#endif

