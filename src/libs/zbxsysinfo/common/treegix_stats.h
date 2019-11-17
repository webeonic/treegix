

#ifndef TREEGIX_SYSINFO_COMMON_TREEGIX_STATS_H_
#define TREEGIX_SYSINFO_COMMON_TREEGIX_STATS_H_

extern char	*CONFIG_SOURCE_IP;
extern int	CONFIG_TIMEOUT;

int	zbx_get_remote_treegix_stats(const char *ip, unsigned short port, AGENT_RESULT *result);
int	zbx_get_remote_treegix_stats_queue(const char *ip, unsigned short port, const char *from, const char *to,
		AGENT_RESULT *result);

int	TREEGIX_STATS(AGENT_REQUEST *request, AGENT_RESULT *result);

#endif /* TREEGIX_SYSINFO_COMMON_TREEGIX_STATS_H_ */
