
package systemrun

import (
	"fmt"
	"time"
	"treegix/internal/agent"
	"treegix/pkg/plugin"
	"treegix/pkg/trxcmd"
)

// Plugin -
type Plugin struct {
	plugin.Base
	enableRemoteCommands int
}

var impl Plugin

func (p *Plugin) Configure(options map[string]string) {
	p.enableRemoteCommands = agent.Options.EnableRemoteCommands
}

// Export -
func (p *Plugin) Export(key string, params []string, ctx plugin.ContextProvider) (result interface{}, err error) {
	if p.enableRemoteCommands != 1 {
		return nil, fmt.Errorf("Remote commands are not enabled.")
	}

	if len(params) > 2 {
		return nil, fmt.Errorf("Too many parameters.")
	}

	if len(params) == 0 || len(params[0]) == 0 {
		return nil, fmt.Errorf("Invalid first parameter.")
	}

	if agent.Options.LogRemoteCommands == 1 {
		p.Warningf("Executing command:'%s'", params[0])
	} else {
		p.Debugf("Executing command:'%s'", params[0])
	}

	if len(params) == 1 || params[1] == "" || params[1] == "wait" {
		stdoutStderr, err := trxcmd.Execute(params[0], time.Second*time.Duration(agent.Options.Timeout))
		if err != nil {
			return nil, err
		}

		p.Debugf("command:'%s' length:%d output:'%.20s'", params[0], len(stdoutStderr), stdoutStderr)

		return stdoutStderr, nil
	} else if params[1] == "nowait" {
		err := trxcmd.ExecuteBackground(params[0])

		if err != nil {
			return nil, err
		}

		return 1, nil
	}

	return nil, fmt.Errorf("Invalid second parameter.")
}

func init() {
	plugin.RegisterMetrics(&impl, "SystemRun", "system.run", "Run specified command.")
}
