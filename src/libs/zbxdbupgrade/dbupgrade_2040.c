

#include "common.h"
#include "db.h"
#include "dbupgrade.h"

/*
 * 2.4 maintenance database patches
 */

#ifndef HAVE_SQLITE3

static int	DBpatch_2040000(void)
{
	return SUCCEED;
}

#endif

DBPATCH_START(2040)

/* version, duplicates flag, mandatory flag */

DBPATCH_ADD(2040000, 0, 1)

DBPATCH_END()
