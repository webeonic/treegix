
package kernel

import (
	"errors"
	"fmt"
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
	var proc bool

	if len(params) > 0 {
		return nil, errors.New("Too many parameters.")
	}

	switch key {
	case "kernel.maxproc":
		proc = true
	case "kernel.maxfiles":
		proc = false
	default:
		/* SHOULD_NEVER_HAPPEN */
		return 0, fmt.Errorf("Unsupported metric.")
	}

	return getMax(proc)
}

func init() {
	stdOs = std.NewOs()
	plugin.RegisterMetrics(&impl, "Kernel",
		"kernel.maxproc", "Returns maximum number of processes supported by OS.",
		"kernel.maxfiles", "Returns maximum number of opened files supported by OS.")
}
