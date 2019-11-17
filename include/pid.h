
#ifndef TREEGIX_PID_H
#define TREEGIX_PID_H

#ifdef _WINDOWS
#	error "This module allowed only for Unix OS"
#endif

int	create_pid_file(const char *pidfile);
int	read_pid_file(const char *pidfile, pid_t *pid, char *error, size_t max_error_len);
void	drop_pid_file(const char *pidfile);

#endif
