

package uptime

import (
	"errors"
	"treegix/pkg/plugin"
	"treegix/pkg/std"
)

// Plugin -
type Plugin struct {
	plugin.Base
}

var impl Plugin
var stdOs std.Os

// Export -
func (p *Plugin) Export(key string, params []string, ctx plugin.ContextProvider) (result interface{}, err error) {
	if len(params) > 0 {
		return nil, errors.New("Too many parameters.")
	}
	return getUptime()
}

func init() {
	stdOs = std.NewOs()
	plugin.RegisterMetrics(&impl, "Uptime", "system.uptime", "Returns system uptime in seconds.")
}
