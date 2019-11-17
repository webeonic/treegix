
#ifndef TREEGIX_TRXEXEC_H
#define TREEGIX_TRXEXEC_H

#define TRX_EXIT_CODE_CHECKS_DISABLED	0
#define TRX_EXIT_CODE_CHECKS_ENABLED	1

int	zbx_execute(const char *command, char **buffer, char *error, size_t max_error_len, int timeout,
		unsigned char flag);
int	zbx_execute_nowait(const char *command);

#endif
