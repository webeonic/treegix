

package treegixasync

import (
	"treegix/pkg/plugin"
	"treegix/pkg/zbxlib"
)

// Plugin -
type Plugin struct {
	plugin.Base
}

var impl Plugin

func (p *Plugin) Export(key string, params []string, ctx plugin.ContextProvider) (result interface{}, err error) {
	return zbxlib.ExecuteCheck(key, params)
}

func init() {
	plugin.RegisterMetrics(&impl, "TreegixAsync",
		"system.localtime", "Returns system local time.",
		"system.boottime", "Returns system boot time.",
		"net.tcp.listen", "Checks if this TCP port is in LISTEN state.",
		"net.udp.listen", "Checks if this UDP port is in LISTEN state.",
		"sensor", "Hardware sensor reading.",
		"system.cpu.load", "CPU load.",
		"system.cpu.switches", "Count of context switches.",
		"system.cpu.intr", "Device interrupts.",
		"system.hw.cpu", "CPU information.",
		"system.hw.macaddr", "Listing of MAC addresses.",
		"system.sw.os", "Operating system information.",
		"system.swap.in", "Swap in (from device into memory) statistics.",
		"system.swap.out", "Swap out (from memory onto device) statistics.",
		"vfs.file.md5sum", "MD5 checksum of file.",
		"vfs.file.regmatch", "Find string in a file.",
		"vfs.fs.discovery", "List of mounted filesystems. Used for low-level discovery.")
}
