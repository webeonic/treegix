

#include "common.h"
#include "db.h"
#include "dbupgrade.h"

extern unsigned char	program_type;

/*
 * 4.2 maintenance database patches
 */

#ifndef HAVE_SQLITE3

static int	DBpatch_4020000(void)
{
	return SUCCEED;
}

#endif

DBPATCH_START(4020)

/* version, duplicates flag, mandatory flag */

DBPATCH_ADD(4020000, 0, 1)

DBPATCH_END()
