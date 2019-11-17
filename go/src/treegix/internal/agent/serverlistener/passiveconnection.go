

package serverlistener

import (
	"time"
	"treegix/internal/agent"
	"treegix/pkg/zbxcomms"
)

type passiveConnection struct {
	conn *zbxcomms.Connection
}

func (pc *passiveConnection) Write(data []byte) (n int, err error) {
	if err = pc.conn.Write(data, time.Second*time.Duration(agent.Options.Timeout)); err != nil {
		n = len(data)
	}
	pc.conn.Close()
	return
}

func (pc *passiveConnection) Address() string {
	return pc.conn.RemoteIP()
}
