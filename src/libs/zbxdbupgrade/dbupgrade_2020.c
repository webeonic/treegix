

#include "common.h"
#include "db.h"
#include "dbupgrade.h"

/*
 * 2.2 maintenance database patches
 */

#ifndef HAVE_SQLITE3

static int	DBpatch_2020000(void)
{
	return SUCCEED;
}

#endif

DBPATCH_START(2020)

/* version, duplicates flag, mandatory flag */

DBPATCH_ADD(2020000, 0, 1)

DBPATCH_END()
