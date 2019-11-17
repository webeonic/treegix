
package proc

type procQuery struct {
	name    string
	user    string
	cmdline string
}

const (
	procInfoPid = 1 << iota
	procInfoName
	procInfoUser
	procInfoCmdline
)

type procInfo struct {
	pid     int64
	name    string
	userid  int64
	cmdline string
	arg0    string
}

type cpuUtil struct {
	utime   uint64
	stime   uint64
	started uint64
	err     error
}
