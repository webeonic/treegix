
#ifndef TREEGIX_DISK_H
#define TREEGIX_DISK_H

#ifndef _WINDOWS
#	error "This module is only available for Windows OS"
#endif

trx_uint64_t	get_cluster_size(const char *path, char **error);

#endif /* TREEGIX_DISK_H */
