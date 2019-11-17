

package netif

import (
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

type dirFlag uint8

const (
	dirIn dirFlag = 1 << iota
	dirOut
)

// Export -
func (p *Plugin) Export(key string, params []string, ctx plugin.ContextProvider) (result interface{}, err error) {
	var direction dirFlag
	var mode string

	switch key {
	case "net.if.discovery":
		if len(params) > 0 {
			return nil, fmt.Errorf("Too many parameters.")
		}
		return getDevList()
	case "net.if.collisions":
		if len(params) > 1 {
			return nil, fmt.Errorf("Too many parameters.")
		}

		if len(params) < 1 || params[0] == "" {
			return nil, fmt.Errorf("Network interface name cannot be empty.")
		}
		return getNetStats(params[0], "collisions", dirOut)
	case "net.if.in":
		direction = dirIn
	case "net.if.out":
		direction = dirOut
	case "net.if.total":
		direction = dirIn | dirOut
	default:
		/* SHOULD_NEVER_HAPPEN */
		return nil, fmt.Errorf("Unsupported metric.")
	}

	if len(params) < 1 || params[0] == "" {
		return nil, fmt.Errorf("Network interface name cannot be empty.")
	}

	if len(params) > 2 {
		return nil, fmt.Errorf("Too many parameters.")
	}

	if len(params) == 2 && params[1] != "" {
		mode = params[1]
	} else {
		mode = "bytes"
	}

	return getNetStats(params[0], mode, direction)
}

func init() {
	stdOs = std.NewOs()

	plugin.RegisterMetrics(&impl, "NetIf",
		"net.if.collisions", "Returns number of out-of-window collisions.",
		"net.if.in", "Returns incoming traffic statistics on network interface.",
		"net.if.out", "Returns outgoing traffic statistics on network interface.",
		"net.if.total", "Returns sum of incoming and outgoing traffic statistics on network interface.",
		"net.if.discovery", "Returns list of network interfaces. Used for low-level discovery.")

}
