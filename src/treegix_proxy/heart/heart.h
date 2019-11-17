

#ifndef TREEGIX_HEART_H
#define TREEGIX_HEART_H

#include "threads.h"

extern int	CONFIG_HEARTBEAT_FREQUENCY;

TRX_THREAD_ENTRY(heart_thread, args);

#endif
