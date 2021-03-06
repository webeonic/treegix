

#ifndef TREEGIX_LOGFILES_H
#define TREEGIX_LOGFILES_H

#include "trxregexp.h"
#include "md5.h"
#include "../metrics.h"

#define TRX_LOG_ROTATION_LOGRT	0	/* pure rotation model */
#define TRX_LOG_ROTATION_LOGCPT	1	/* copy-truncate rotation model */

struct	st_logfile
{
	char		*filename;
	int		mtime;		/* st_mtime from stat() */
	int		md5size;	/* size of the initial part for which the md5 sum is calculated */
	int		seq;		/* number in processing order */
	int		retry;
	int		incomplete;	/* 0 - the last record ends with a newline, 1 - the last record contains */
					/* no newline at the end */
	int		copy_of;	/* '-1' - the file is not a copy. '0 <= copy_of' - this file is a copy of */
					/* the file with index 'copy_of' in the old log file list. */
	trx_uint64_t	dev;		/* ID of device containing file */
	trx_uint64_t	ino_lo;		/* UNIX: inode number. Microsoft Windows: nFileIndexLow or FileId.LowPart */
	trx_uint64_t	ino_hi;		/* Microsoft Windows: nFileIndexHigh or FileId.HighPart */
	trx_uint64_t	size;		/* st_size from stat() */
	trx_uint64_t	processed_size;	/* how far the Treegix agent has analyzed the file */
	md5_byte_t	md5buf[MD5_DIGEST_SIZE];	/* md5 sum of the initial part of the file */
};

typedef int (*trx_process_value_func_t)(const char *, unsigned short, const char *, const char *, const char *,
		unsigned char, trx_uint64_t *, const int *, unsigned long *, const char *, unsigned short *,
		unsigned long *, unsigned char);

void	destroy_logfile_list(struct st_logfile **logfiles, int *logfiles_alloc, int *logfiles_num);

int	process_logrt(unsigned char flags, const char *filename, trx_uint64_t *lastlogsize, int *mtime,
		trx_uint64_t *lastlogsize_sent, int *mtime_sent, unsigned char *skip_old_data, int *big_rec,
		int *use_ino, char **err_msg, struct st_logfile **logfiles_old, const int *logfiles_num_old,
		struct st_logfile **logfiles_new, int *logfiles_num_new, const char *encoding,
		trx_vector_ptr_t *regexps, const char *pattern, const char *output_template, int *p_count, int *s_count,
		trx_process_value_func_t process_value, const char *server, unsigned short port, const char *hostname,
		const char *key, int *jumped, float max_delay, double *start_time, trx_uint64_t *processed_bytes,
		int rotation_type);

int	process_log_check(char *server, unsigned short port, trx_vector_ptr_t *regexps, TRX_ACTIVE_METRIC *metric,
		trx_process_value_func_t process_value_cb, trx_uint64_t *lastlogsize_sent, int *mtime_sent,
		char **error);

#endif
