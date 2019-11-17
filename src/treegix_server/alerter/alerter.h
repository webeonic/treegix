

#ifndef TREEGIX_ALERTER_H
#define TREEGIX_ALERTER_H

#include "db.h"
#include "threads.h"

extern char	*CONFIG_ALERT_SCRIPTS_PATH;

TRX_THREAD_ENTRY(alerter_thread, args);

#endif
