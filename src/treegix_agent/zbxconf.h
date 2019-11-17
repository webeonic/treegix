

#ifndef TREEGIX_ZBXCONF_H
#define TREEGIX_ZBXCONF_H

extern char	*CONFIG_HOSTS_ALLOWED;
extern char	*CONFIG_HOSTNAME;
extern char	*CONFIG_HOSTNAME_ITEM;
extern char	*CONFIG_HOST_METADATA;
extern char	*CONFIG_HOST_METADATA_ITEM;
extern int	CONFIG_ENABLE_REMOTE_COMMANDS;
extern int	CONFIG_UNSAFE_USER_PARAMETERS;
extern int	CONFIG_LISTEN_PORT;
extern int	CONFIG_REFRESH_ACTIVE_CHECKS;
extern char	*CONFIG_LISTEN_IP;
extern int	CONFIG_LOG_LEVEL;
extern int	CONFIG_MAX_LINES_PER_SECOND;
extern char	**CONFIG_ALIASES;
extern char	**CONFIG_USER_PARAMETERS;
extern char	*CONFIG_LOAD_MODULE_PATH;
extern char	**CONFIG_LOAD_MODULE;
#ifdef _WINDOWS
extern char	**CONFIG_PERF_COUNTERS;
extern char	**CONFIG_PERF_COUNTERS_EN;
#endif
extern char	*CONFIG_USER;

extern unsigned int	configured_tls_connect_mode;
extern unsigned int	configured_tls_accept_modes;

extern char	*CONFIG_TLS_CONNECT;
extern char	*CONFIG_TLS_ACCEPT;
extern char	*CONFIG_TLS_CA_FILE;
extern char	*CONFIG_TLS_CRL_FILE;
extern char	*CONFIG_TLS_SERVER_CERT_ISSUER;
extern char	*CONFIG_TLS_SERVER_CERT_SUBJECT;
extern char	*CONFIG_TLS_CERT_FILE;
extern char	*CONFIG_TLS_KEY_FILE;
extern char	*CONFIG_TLS_PSK_IDENTITY;
extern char	*CONFIG_TLS_PSK_FILE;

void	load_aliases(char **lines);
void	load_user_parameters(char **lines);
#ifdef _WINDOWS
void	load_perf_counters(const char **def_lines, const char **eng_lines);
#endif

#ifdef _AIX
void	tl_version(void);
#endif

#endif /* TREEGIX_ZBXCONF_H */
