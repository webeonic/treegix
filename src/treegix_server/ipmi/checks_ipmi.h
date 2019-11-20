

#ifndef TREEGIX_CHECKS_IPMI_H
#define TREEGIX_CHECKS_IPMI_H

#include "common.h"

#ifdef HAVE_OPENIPMI

#include "dbcache.h"
#include "sysinfo.h"

int	trx_init_ipmi_handler(void);
void	trx_free_ipmi_handler(void);

int	get_value_ipmi(trx_uint64_t itemid, const char *addr, unsigned short port, signed char authtype,
		unsigned char privilege, const char *username, const char *password, const char *sensor, char **value);

int	trx_parse_ipmi_command(const char *command, char *c_name, int *val, char *error, size_t max_error_len);

int	trx_set_ipmi_control_value(trx_uint64_t hostid, const char *addr, unsigned short port, signed char authtype,
		unsigned char privilege, const char *username, const char *password, const char *sensor,
		int value, char **error);

void	trx_delete_inactive_ipmi_hosts(time_t last_check);

void	trx_perform_all_openipmi_ops(int timeout);

#endif	/* HAVE_OPENIPMI */

#endif	/* TREEGIX_CHECKS_IPMI_H */
