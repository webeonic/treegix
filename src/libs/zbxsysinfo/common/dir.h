

#ifndef TREEGIX_SYSINFO_COMMON_DIR_H
#define TREEGIX_SYSINFO_COMMON_DIR_H

#include "sysinfo.h"

#define DISK_BLOCK_SIZE			512	/* 512-byte blocks */

#define SIZE_MODE_APPARENT		0	/* bytes in file */
#define SIZE_MODE_DISK			1	/* size on disk */

#define TRAVERSAL_DEPTH_UNLIMITED	-1	/* directory traversal depth is not limited */

typedef struct
{
	int depth;
	char *path;
} trx_directory_item_t;

typedef struct
{
	trx_uint64_t st_dev;			/* device */
	trx_uint64_t st_ino;			/* file serial number */
} trx_file_descriptor_t;

int	VFS_DIR_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result);
int	VFS_DIR_COUNT(AGENT_REQUEST *request, AGENT_RESULT *result);

#endif /* TREEGIX_SYSINFO_COMMON_DIR_H */
