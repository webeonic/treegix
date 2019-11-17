

package uname

import (
	"errors"
	"treegix/pkg/plugin"
)

// Plugin -
type Plugin struct {
	plugin.Base
}

var impl Plugin

// Export -
func (p *Plugin) Export(key string, params []string, ctx plugin.ContextProvider) (result interface{}, err error) {
	if len(params) > 0 {
		return nil, errors.New("Too many parameters.")
	}

	switch key {
	case "system.uname":
		return getUname()
	case "system.hostname":
		return getHostname()
	case "system.sw.arch":
		return getSwArch()
	default:
		return nil, errors.New("Unsupported metric.")
	}

}

func init() {
	plugin.RegisterMetrics(&impl, "Uname",
		"system.uname", "Returns system uname.",
		"system.hostname", "Returns system host name.",
		"system.sw.arch", "Software architecture information.")
}
