

package log

import (
	"time"
	"treegix/pkg/plugin"
)

// Plugin -
type Plugin struct {
	plugin.Base
	input   chan *watchRequest
	clients map[plugin.ResultWriter][]*plugin.Request
}

type watchRequest struct {
	requests []*plugin.Request
	sink     plugin.ResultWriter
}

var impl Plugin

func (p *Plugin) run() {
	p.Debugf("activating plugin")
	ticker := time.NewTicker(time.Second)

run:
	for {
		select {
		case <-ticker.C:
			for sink, requests := range p.clients {
				for _, r := range requests {
					now := time.Now()
					value := now.Format(time.Stamp)
					lastlogsize := uint64(now.UnixNano())
					mtime := int(now.Unix())
					sink.Write(&plugin.Result{
						Itemid:      r.Itemid,
						Value:       &value,
						LastLogsize: &lastlogsize,
						Ts:          now,
						Mtime:       &mtime})
				}
			}
		case wr := <-p.input:
			if wr == nil {
				break run
			}
			p.clients[wr.sink] = wr.requests
		}
	}

	p.Debugf("plugin deactivated")
}

func (p *Plugin) Start() {
	p.Debugf("start")
	p.input = make(chan *watchRequest)
	p.clients = make(map[plugin.ResultWriter][]*plugin.Request)
	go p.run()
}

func (p *Plugin) Stop() {
	p.Debugf("stop")
	close(p.input)
}

func (p *Plugin) Watch(requests []*plugin.Request, ctx plugin.ContextProvider) {
	p.Debugf("watch")
	p.input <- &watchRequest{sink: ctx.Output(), requests: requests}
}

func (p *Plugin) Configure(options map[string]string) {
	p.Debugf("configure")
}

func init() {
	plugin.RegisterMetrics(&impl, "DebugLog", "debug.log", "Returns timestamp each second.")
}
