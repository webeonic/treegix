

#include "common.h"
#include "log.h"

/******************************************************************************
 *                                                                            *
 * Function: get_cluster_size                                                 *
 *                                                                            *
 * Purpose: get file system cluster size for specified path (for cases when   *
 *          the file system is mounted on empty NTFS directory)               *
 *                                                                            *
 * Parameters: path  - [IN] file system path                                  *
 *             error - [OUT] error message                                    *
 *                                                                            *
 * Return value: On success, nonzero cluster size is returned                 *
 *               On error, 0 is returned.                                     *
 *                                                                            *
 ******************************************************************************/
trx_uint64_t	get_cluster_size(const char *path, char **error)
{
	wchar_t 	*disk = NULL, *wpath = NULL;
	unsigned long	sectors_per_cluster, bytes_per_sector, path_length;
	trx_uint64_t	res = 0;
	char		*err_msg = "Cannot obtain file system cluster size:";

	wpath = trx_utf8_to_unicode(path);

	/* Here GetFullPathName() is used in multithreaded application. */
	/* We assume it is safe because: */
	/*   - only file names with absolute paths are used (i.e. no relative paths) and */
	/*   - SetCurrentDirectory() is not used in Treegix agent. */

	if (0 == (path_length = GetFullPathName(wpath, 0, NULL, NULL) + 1))
	{
		*error = trx_dsprintf(*error, "%s GetFullPathName() failed: %s", err_msg,
				strerror_from_system(GetLastError()));
		goto err;
	}

	disk = (wchar_t *)trx_malloc(NULL, path_length * sizeof(wchar_t));

	if (0 == GetVolumePathName(wpath, disk, path_length))
	{
		*error = trx_dsprintf(*error, "%s GetVolumePathName() failed: %s", err_msg,
				strerror_from_system(GetLastError()));
		goto err;
	}

	if (0 == GetDiskFreeSpace(disk, &sectors_per_cluster, &bytes_per_sector, NULL, NULL))
	{
		*error = trx_dsprintf(*error, "%s GetDiskFreeSpace() failed: %s", err_msg,
				strerror_from_system(GetLastError()));
		goto err;
	}

	res = (trx_uint64_t)sectors_per_cluster * bytes_per_sector;
err:
	trx_free(disk);
	trx_free(wpath);

	return res;
}
