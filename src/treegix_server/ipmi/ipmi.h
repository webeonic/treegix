

#ifndef TREEGIX_IPMI_H
#define TREEGIX_IPMI_H

#include "checks_ipmi.h"

int	trx_ipmi_port_expand_macros(trx_uint64_t hostid, const char *port_orig, unsigned short *port, char **error);
int	trx_ipmi_execute_command(const DC_HOST *host, const char *command, char *error, size_t max_error_len);

#endif
