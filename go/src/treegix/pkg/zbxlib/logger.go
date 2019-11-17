

package zbxlib

import (
	"C"
)
import "treegix/pkg/log"

//export handleTreegixLog
func handleTreegixLog(clevel C.int, cmessage *C.char) {
	message := C.GoString(cmessage)
	switch int(clevel) {
	case log.Empty:
	case log.Crit:
		log.Critf(message)
	case log.Err:
		log.Errf(message)
	case log.Warning:
		log.Warningf(message)
	case log.Debug:
		log.Debugf(message)
	case log.Trace:
		log.Tracef(message)
	case log.Info:
		log.Infof(message)
	}
}
