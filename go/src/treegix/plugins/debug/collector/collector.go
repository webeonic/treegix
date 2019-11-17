

package empty

import (
	"strconv"
	"treegix/pkg/plugin"
	"treegix/pkg/std"
)

// Plugin -
type Plugin struct {
	plugin.Base
	interval int
	counter  int
}

var impl Plugin
var stdOs std.Os

func (p *Plugin) Export(key string, params []string, ctx plugin.ContextProvider) (result interface{}, err error) {
	p.Debugf("export %s%v", key, params)
	return p.counter, nil
}

func (p *Plugin) Collect() error {
	p.Debugf("collect")
	p.counter++
	return nil
}

func (p *Plugin) Period() int {
	return p.interval
}

func (p *Plugin) Configure(options map[string]string) {
	p.Debugf("configure")
	p.interval = 10
	if options != nil {
		if val, ok := options["Interval"]; ok {
			p.interval, _ = strconv.Atoi(val)
		}
	}
}

func init() {
	stdOs = std.NewOs()
	impl.interval = 1
	plugin.RegisterMetrics(&impl, "DebugCollector", "debug.collector", "Returns empty value.")
}
