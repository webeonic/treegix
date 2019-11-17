

#ifndef TREEGIX_SOFTWARE_H
#define TREEGIX_SOFTWARE_H

#define SW_OS_FULL			"/proc/version"
#define SW_OS_SHORT 			"/proc/version_signature"
#define SW_OS_NAME			"/etc/issue.net"
#define SW_OS_NAME_RELEASE		"/etc/os-release"

#define SW_OS_OPTION_PRETTY_NAME	"PRETTY_NAME"

typedef struct
{
	const char	*name;
	const char	*test_cmd;	/* if this shell command has stdout output, package manager is present */
	const char	*list_cmd;	/* this command lists the installed packages */
	int		(*parser)(const char *line, char *package, size_t max_package_len);	/* for non-standard list (package per line), add a parser function */
}
ZBX_PACKAGE_MANAGER;

#endif	/* TREEGIX_SOFTWARE_H */
