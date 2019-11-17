

package file

//#include <iconv.h>
//#include <stdlib.h>
//size_t call_iconv(iconv_t cd, char *inbuf, size_t *inbytesleft, char *outbuf, size_t *outbytesleft) {
//   return iconv(cd, &inbuf, inbytesleft, &outbuf, outbytesleft);
// }
import "C"

import (
	"syscall"
	"unsafe"
)

func decode(encoder string, inbuf []byte) (outbuf []byte) {

	if "" == encoder {
		return inbuf
	}

	tocode := C.CString("UTF-8")
	defer C.free(unsafe.Pointer(tocode))
	fromcode := C.CString(encoder)
	defer C.free(unsafe.Pointer(fromcode))

	cd, err := C.iconv_open(tocode, fromcode)

	if err != nil {
		return inbuf
	}

	outbuf = make([]byte, len(inbuf))
	inbytes := C.size_t(len(inbuf))
	outbytes := C.size_t(len(inbuf))

	for {
		inptr := (*C.char)(unsafe.Pointer(&inbuf[len(inbuf)-int(inbytes)]))
		outptr := (*C.char)(unsafe.Pointer(&outbuf[len(outbuf)-int(outbytes)]))
		_, err := C.call_iconv(cd, inptr, &inbytes, outptr, &outbytes)
		if err == nil || err.(syscall.Errno) != syscall.E2BIG {
			break
		}
		outbytes += C.size_t(len(inbuf))
		tmp := make([]byte, len(outbuf)+len(inbuf))
		copy(tmp, outbuf)
		outbuf = tmp
	}
	outbuf = outbuf[:len(outbuf)-int(outbytes)]
	C.iconv_close(cd)
	return
}
