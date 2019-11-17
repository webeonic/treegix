

#include "common.h"
#include "db.h"
#include "dbupgrade.h"

/*
 * 3.0 maintenance database patches
 */

#ifndef HAVE_SQLITE3

static int	DBpatch_3000000(void)
{
	return SUCCEED;
}

#endif

DBPATCH_START(3000)

/* version, duplicates flag, mandatory flag */

DBPATCH_ADD(3000000, 0, 1)

DBPATCH_END()
