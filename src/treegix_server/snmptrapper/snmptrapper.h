

#ifndef TREEGIX_SNMPTRAPPER_H
#define TREEGIX_SNMPTRAPPER_H

#include "threads.h"

extern char		*CONFIG_SNMPTRAP_FILE;
extern unsigned char	process_type;

ZBX_THREAD_ENTRY(snmptrapper_thread, args);

#endif
