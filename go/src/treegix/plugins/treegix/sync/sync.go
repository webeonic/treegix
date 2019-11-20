

package treegixsync

import (
	"treegix/pkg/plugin"
	"treegix/pkg/trxlib"
)

// Plugin -
type Plugin struct {
	plugin.Base
}

var impl Plugin

func (p *Plugin) Export(key string, params []string, ctx plugin.ContextProvider) (result interface{}, err error) {
	return trxlib.ExecuteCheck(key, params)
}

func init() {
	plugin.RegisterMetrics(&impl, "TreegixSync",
		"net.dns", "Checks if DNS service is up.",
		"net.dns.record", "Performs DNS query.",
		"proc.mem", "Memory used by process in bytes.",
		"proc.num", "The number of processes.",
		"web.page.get", "Get content of web page.",
		"web.page.perf", "Loading time of full web page (in seconds).",
		"web.page.regexp", "Find string on a web page.",
		"system.hw.chassis", "Chassis information.",
		"system.hw.devices", "Listing of PCI or USB devices.",
		"system.sw.packages", "Listing of installed packages.",
		"net.tcp.port", "Checks if it is possible to make TCP connection to specified port.",
		"net.tcp.service", "Checks if service is running and accepting TCP connections.",
		"net.tcp.service.perf", "Checks performance of TCP service.",
		"net.udp.service", "Checks if service is running and responding to UDP requests.",
		"net.udp.service.perf", "Checks performance of UDP service.",
		"system.users.num", "Number of users logged in.",
		"system.swap.size", "Swap space size in bytes or in percentage from total.",
		"vfs.dir.count", "Directory entry count.",
		"vfs.dir.size", "Directory size (in bytes).",
		"vfs.fs.inode", "Number or percentage of inodes.",
		"vfs.fs.size", "Disk space in bytes or in percentage from total.",
		"vm.memory.size", "Memory size in bytes or in percentage from total.")
	impl.SetCapacity(1)
}
