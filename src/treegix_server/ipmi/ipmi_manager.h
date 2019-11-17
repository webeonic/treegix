

#ifndef TREEGIX_IPMI_MANAGER_H
#define TREEGIX_IPMI_MANAGER_H

#include "common.h"

#ifdef HAVE_OPENIPMI

#include "threads.h"

TRX_THREAD_ENTRY(ipmi_manager_thread, args);

#endif

#endif
