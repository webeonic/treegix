

#ifndef TREEGIX_PROXYCONFIG_H
#define TREEGIX_PROXYCONFIG_H

#include "threads.h"

extern int	CONFIG_PROXYCONFIG_FREQUENCY;

TRX_THREAD_ENTRY(proxyconfig_thread, args);

#endif
