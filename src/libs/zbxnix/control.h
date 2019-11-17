

#ifndef TREEGIX_CONTROL_H
#define TREEGIX_CONTROL_H

#include "common.h"

#define ZBX_RTC_LOG_SCOPE_FLAG	0x80
#define ZBX_RTC_LOG_SCOPE_PROC	0
#define ZBX_RTC_LOG_SCOPE_PID	1

int	parse_rtc_options(const char *opt, unsigned char program_type, int *message);

#endif
