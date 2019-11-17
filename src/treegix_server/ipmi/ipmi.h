

#ifndef TREEGIX_IPMI_H
#define TREEGIX_IPMI_H

#include "checks_ipmi.h"

int	zbx_ipmi_port_expand_macros(zbx_uint64_t hostid, const char *port_orig, unsigned short *port, char **error);
int	zbx_ipmi_execute_command(const DC_HOST *host, const char *command, char *error, size_t max_error_len);

#endif
