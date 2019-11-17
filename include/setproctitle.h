
#ifndef TREEGIX_SETPROCTITLE_H
#define TREEGIX_SETPROCTITLE_H

#if defined(__linux__)				/* Linux */
#       define PS_OVERWRITE_ARGV
#elif defined(_AIX)				/* AIX */
#       define PS_OVERWRITE_ARGV
#       define PS_CONCAT_ARGV
#elif defined(__sun) && defined(__SVR4)		/* Solaris */
#       define PS_OVERWRITE_ARGV
#       define PS_APPEND_ARGV
#elif defined(HAVE_SYS_PSTAT_H)			/* HP-UX */
#       define PS_PSTAT_ARGV
#elif defined(__APPLE__) && defined(__MACH__)	/* OS X */
#	include <TargetConditionals.h>
#	if TARGET_OS_MAC == 1 && TARGET_OS_EMBEDDED == 0 && TARGET_OS_IPHONE == 0 && TARGET_IPHONE_SIMULATOR == 0
#		define PS_OVERWRITE_ARGV
#		define PS_DARWIN_ARGV
#	endif
#endif

char	**setproctitle_save_env(int argc, char **argv);
void	setproctitle_set_status(const char *status);
void	setproctitle_free_env(void);

#endif	/* TREEGIX_SETPROCTITLE_H */
