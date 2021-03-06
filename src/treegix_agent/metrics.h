

#ifndef TREEGIX_METRICS_H
#define TREEGIX_METRICS_H

#include "trxtypes.h"

/* define minimal and maximal values of lines to send by agent */
/* per second for checks `log' and `eventlog', used to parse key parameters */
#define	MIN_VALUE_LINES			1
#define	MAX_VALUE_LINES			1000
#define	MAX_VALUE_LINES_MULTIPLIER	10

/* NB! Next list must fit in unsigned char (see TRX_ACTIVE_METRIC "flags" field below). */
#define TRX_METRIC_FLAG_PERSISTENT	0x01	/* do not overwrite old values when adding to the buffer */
#define TRX_METRIC_FLAG_NEW		0x02	/* new metric, just added */
#define TRX_METRIC_FLAG_LOG_LOG		0x04	/* log[ or log.count[, depending on TRX_METRIC_FLAG_LOG_COUNT */
#define TRX_METRIC_FLAG_LOG_LOGRT	0x08	/* logrt[ or logrt.count[, depending on TRX_METRIC_FLAG_LOG_COUNT */
#define TRX_METRIC_FLAG_LOG_EVENTLOG	0x10	/* eventlog[ */
#define TRX_METRIC_FLAG_LOG_COUNT	0x20	/* log.count[ or logrt.count[ */
#define TRX_METRIC_FLAG_LOG			/* item for log file monitoring, one of the above */	\
		(TRX_METRIC_FLAG_LOG_LOG | TRX_METRIC_FLAG_LOG_LOGRT | TRX_METRIC_FLAG_LOG_EVENTLOG)

typedef struct
{
	char			*key;
	char			*key_orig;
	trx_uint64_t		lastlogsize;
	int			refresh;
	int			nextcheck;
	int			mtime;
	unsigned char		skip_old_data;	/* for processing [event]log metrics */
	unsigned char		flags;
	unsigned char		state;
	unsigned char		refresh_unsupported;	/* re-check notsupported item */
	int			big_rec;	/* for logfile reading: 0 - normal record, 1 - long unfinished record */
	int			use_ino;	/* 0 - do not use inodes (on FAT, FAT32) */
						/* 1 - use inodes (up to 64-bit) (various UNIX file systems, NTFS) */
						/* 2 - use 128-bit FileID (currently only on ReFS) to identify files */
						/* on a file system */
	int			error_count;	/* number of file reading errors in consecutive checks */
	int			logfiles_num;
	struct st_logfile	*logfiles;	/* for handling of logfile rotation for logrt[], logrt.count[] items */
	double			start_time;	/* Start time of check for log[], log.count[], logrt[], logrt.count[] */
						/* items. Used for measuring duration of checks. */
	trx_uint64_t		processed_bytes;	/* number of processed bytes for log[], log.count[], logrt[], */
							/* logrt.count[] items */
}
TRX_ACTIVE_METRIC;

#endif
