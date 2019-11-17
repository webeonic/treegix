

package serverlistener

import (
	"time"
	"treegix/internal/agent"
	"treegix/internal/agent/scheduler"
	"treegix/pkg/log"
)

const notsupported = "TRX_NOTSUPPORTED"

type passiveCheck struct {
	conn      *passiveConnection
	scheduler scheduler.Scheduler
}

func (pc *passiveCheck) formatError(msg string) (data []byte) {
	data = make([]byte, len(notsupported)+len(msg)+1)
	copy(data, notsupported)
	copy(data[len(notsupported)+1:], msg)
	return
}

func (pc *passiveCheck) handleCheck(data []byte) {
	s, err := pc.scheduler.PerformTask(string(data), time.Second*time.Duration(agent.Options.Timeout))

	if err != nil {
		log.Debugf("sending passive check response: %s: '%s' to '%s'", notsupported, err.Error(), pc.conn.Address())
		_, err = pc.conn.Write(pc.formatError(err.Error()))
	} else {
		log.Debugf("sending passive check response: '%s' to '%s'", s, pc.conn.Address())
		_, err = pc.conn.Write([]byte(s))
	}

	if err != nil {
		log.Debugf("could not send response to server '%s': %s", pc.conn.Address(), err.Error())
	}
}
