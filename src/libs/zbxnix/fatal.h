

#ifndef TREEGIX_FATAL_H
#define TREEGIX_FATAL_H

#include <signal.h>

#define ZBX_FATAL_LOG_PC_REG_SF		0x0001
#define ZBX_FATAL_LOG_BACKTRACE		0x0002
#define ZBX_FATAL_LOG_MEM_MAP		0x0004
#define ZBX_FATAL_LOG_FULL_INFO		(ZBX_FATAL_LOG_PC_REG_SF | ZBX_FATAL_LOG_BACKTRACE | ZBX_FATAL_LOG_MEM_MAP)

const char	*get_signal_name(int sig);
void	zbx_log_fatal_info(void *context, unsigned int flags);

#endif
