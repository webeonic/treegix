

#ifndef TREEGIX_VALUECACHE_H
#define TREEGIX_VALUECACHE_H

#include "trxtypes.h"
#include "trxalgo.h"
#include "trxhistory.h"

/*
 * The Value Cache provides read caching of item historical data residing in history
 * tables. No components must read history tables manually. Instead all history data
 * must be read from the Value Cache.
 *
 * Usage notes:
 *
 * Initialization
 *
 *   The value cache must be initialized at the start of the program with trx_vc_init()
 *   function. To ensure proper removal of shared memory the value cache must be destroyed
 *   upon a program exit with trx_vc_destroy() function.
 *
 * Adding data
 *
 *   Whenever a new item value is added to system (history tables) the item value must be
 *   also added added to Value Cache with trx_dc_add_value() function to keep it up to date.
 *
 * Retrieving data
 *
 *   The history data is accessed with trx_vc_get_values() and trx_vc_get_value()
 *   functions. Afterwards the retrieved history data must be freed by the caller by using
 *   either trx_history_record_vector_destroy() function (free the trx_vc_get_values()
 *   call output) or trx_history_record_clear() function (free the trx_vc_get_value() call output).
 *
 * Locking
 *
 *   The cache ensures synchronization between processes by using automatic locks whenever
 *   a cache function (trx_vc_*) is called and by providing manual cache locking functionality
 *   with trx_vc_lock()/trx_vc_unlock() functions.
 *
 */

#define TRX_VC_MODE_NORMAL	0
#define TRX_VC_MODE_LOWMEM	1

/* indicates that all values from database are cached */
#define TRX_ITEM_STATUS_CACHED_ALL	1

/* the cache statistics */
typedef struct
{
	/* Value cache misses are new values cached during request and hits are calculated by  */
	/* subtracting misses from the total number of values returned (0 if the number of     */
	/* returned values is less than misses.                                                */
	/* When performing count based requests the number of cached values might be greater   */
	/* than number of returned values. This can skew the hits/misses ratio towards misses. */
	trx_uint64_t	hits;
	trx_uint64_t	misses;

	trx_uint64_t	total_size;
	trx_uint64_t	free_size;

	/* value cache operating mode - see TRX_VC_MODE_* defines */
	int		mode;
}
trx_vc_stats_t;

int	trx_vc_init(char **error);

void	trx_vc_destroy(void);

void	trx_vc_reset(void);

void	trx_vc_lock(void);

void	trx_vc_unlock(void);

void	trx_vc_enable(void);

void	trx_vc_disable(void);

int	trx_vc_get_values(trx_uint64_t itemid, int value_type, trx_vector_history_record_t *values, int seconds,
		int count, const trx_timespec_t *ts);

int	trx_vc_get_value(trx_uint64_t itemid, int value_type, const trx_timespec_t *ts, trx_history_record_t *value);

int	trx_vc_add_values(trx_vector_ptr_t *history);

int	trx_vc_get_statistics(trx_vc_stats_t *stats);

#endif	/* TREEGIX_VALUECACHE_H */
