

package agent

import (
	"errors"
	"fmt"
	"treegix/pkg/plugin"
	"treegix/pkg/version"
)

// Plugin -
type Plugin struct {
	plugin.Base
}

var impl Plugin

// Export -
func (p *Plugin) Export(key string, params []string, ctx plugin.ContextProvider) (result interface{}, err error) {
	if len(params) > 0 {
		return nil, errors.New("Too many parameters")
	}

	switch key {
	case "agent.hostname":
		return Options.Hostname, nil
	case "agent.ping":
		return 1, nil
	case "agent.version":
		return version.Long(), nil
	}

	return nil, fmt.Errorf("Not implemented: %s", key)
}

func init() {
	plugin.RegisterMetrics(&impl, "Agent",
		"agent.hostname", "Returns Hostname from agent configuration.",
		"agent.ping", "Returns agent availability check result.",
		"agent.version", "Version of Treegix agent.")
}
