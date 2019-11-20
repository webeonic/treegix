

package trxlib

/*
#cgo CFLAGS: -I${SRCDIR}/../../../../../include

#include "common.h"

int	trx_get_agent_item_nextcheck(trx_uint64_t itemid, const char *delay, unsigned char state, int now,
		int refresh_unsupported, int *nextcheck, char **error);
*/
import "C"

import (
	"errors"
	"time"
	"unsafe"
)

func GetNextcheck(itemid uint64, delay string, from time.Time, unsupported bool, refresh_unsupported int) (nextcheck time.Time, err error) {
	var cnextcheck C.int
	var cerr *C.char
	var state int
	cdelay := C.CString(delay)

	if unsupported {
		state = ItemStateNotsupported
	} else {
		state = ItemStateNormal
	}
	now := from.Unix()
	ret := C.trx_get_agent_item_nextcheck(C.ulong(itemid), cdelay, C.uchar(state), C.int(now),
		C.int(refresh_unsupported), &cnextcheck, &cerr)

	if ret != Succeed {
		err = errors.New(C.GoString(cerr))
		C.free(unsafe.Pointer(cerr))
	} else {
		nextcheck = time.Unix(int64(cnextcheck), 0)
	}
	C.free(unsafe.Pointer(cdelay))

	return
}
