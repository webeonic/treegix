

package empty

import (
	"treegix/pkg/plugin"
	"treegix/pkg/std"
)

// Plugin -
type Plugin struct {
	plugin.Base
}

var impl Plugin
var stdOs std.Os

func (p *Plugin) Export(key string, params []string, ctx plugin.ContextProvider) (result interface{}, err error) {
	p.Debugf("export %s%v", key, params)
	return &plugin.Result{}, nil
}

func init() {
	stdOs = std.NewOs()
	plugin.RegisterMetrics(&impl, "DebugEmpty", "debug.empty", "Returns empty value.")
}
