

#include "common.h"
#include "log.h"
#include "trxnix.h"

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
/******************************************************************************
 *                                                                            *
 * Function: trx_coredump_disable                                             *
 *                                                                            *
 * Purpose: disable core dump                                                 *
 *                                                                            *
 * Return value: SUCCEED - core dump disabled                                 *
 *               FAIL - error                                                 *
 *                                                                            *
 ******************************************************************************/
int	trx_coredump_disable(void)
{
	struct rlimit	limit;

	limit.rlim_cur = 0;
	limit.rlim_max = 0;

	if (0 != setrlimit(RLIMIT_CORE, &limit))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot set resource limit: %s", trx_strerror(errno));
		return FAIL;
	}

	return SUCCEED;
}
#endif
