

#ifndef TREEGIX_IPMI_POLLER_H
#define TREEGIX_IPMI_POLLER_H

#include "common.h"

#ifdef HAVE_OPENIPMI

#include "threads.h"

TRX_THREAD_ENTRY(ipmi_poller_thread, args);

#endif

#endif
