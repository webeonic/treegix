

package file

import (
	"errors"
	"time"
	"treegix/internal/agent"
	"treegix/pkg/plugin"
	"treegix/pkg/std"
)

// Plugin -
type Plugin struct {
	plugin.Base
	timeout time.Duration
}

var impl Plugin

// Export -
func (p *Plugin) Export(key string, params []string, ctx plugin.ContextProvider) (result interface{}, err error) {
	switch key {
	case "vfs.file.cksum":
		return p.exportCksum(params)
	case "vfs.file.contents":
		return p.exportContents(params)
	case "vfs.file.exists":
		return p.exportExists(params)
	case "vfs.file.size":
		return p.exportSize(params)
	case "vfs.file.time":
		return p.exportTime(params)
	case "vfs.file.regexp":
		return p.exportRegexp(params)
	default:
		return nil, errors.New("Unsupported metric.")
	}
}

func (p *Plugin) Configure(options map[string]string) {
	p.timeout = time.Duration(agent.Options.Timeout) * time.Second
}

var stdOs std.Os

func init() {
	stdOs = std.NewOs()
	plugin.RegisterMetrics(&impl, "File",
		"vfs.file.cksum", "Returns File checksum, calculated by the UNIX cksum algorithm.",
		"vfs.file.contents", "Retrieves contents of the file.",
		"vfs.file.exists", "Returns if file exists or not.",
		"vfs.file.time", "Returns file time information.",
		"vfs.file.size", "Returns file size.",
		"vfs.file.regexp", "Find string in a file.")
}
