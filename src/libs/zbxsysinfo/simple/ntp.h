

#ifndef TREEGIX_SYSINFO_SIMPLE_NTP_H
#define TREEGIX_SYSINFO_SIMPLE_NTP_H

extern char	*CONFIG_SOURCE_IP;

int	check_ntp(char *host, unsigned short port, int timeout, int *value_int);

#endif /* TREEGIX_SYSINFO_SIMPLE_NTP_H */
