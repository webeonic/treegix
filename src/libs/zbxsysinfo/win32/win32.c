

#include "common.h"
#include "sysinfo.h"

#include "service.h"

ZBX_METRIC	parameters_specific[] =
/*	KEY			FLAG		FUNCTION		TEST PARAMETERS */
{
	{"vfs.fs.size",		CF_HAVEPARAMS,	VFS_FS_SIZE,		"c:,free"},
	{"vfs.fs.discovery",	0,		VFS_FS_DISCOVERY,	NULL},

	{"net.tcp.listen",	CF_HAVEPARAMS,	NET_TCP_LISTEN,		"80"},

	{"net.if.in",		CF_HAVEPARAMS,	NET_IF_IN,		"MS TCP Loopback interface,bytes"},
	{"net.if.out",		CF_HAVEPARAMS,	NET_IF_OUT,		"MS TCP Loopback interface,bytes"},
	{"net.if.total",	CF_HAVEPARAMS,	NET_IF_TOTAL,		"MS TCP Loopback interface,bytes"},
	{"net.if.discovery",	0,		NET_IF_DISCOVERY,	NULL},
	{"net.if.list",		0,		NET_IF_LIST,		NULL},

	{"vm.memory.size",	CF_HAVEPARAMS,	VM_MEMORY_SIZE,		"free"},

	{"proc.num",		CF_HAVEPARAMS,	PROC_NUM,		"svchost.exe"},

	{"system.cpu.util",	CF_HAVEPARAMS,	SYSTEM_CPU_UTIL,	"all,system,avg1"},
	{"system.cpu.load",	CF_HAVEPARAMS,	SYSTEM_CPU_LOAD,	"all,avg1"},
	{"system.cpu.num",	CF_HAVEPARAMS,	SYSTEM_CPU_NUM,		"online"},
	{"system.cpu.discovery",0,		SYSTEM_CPU_DISCOVERY,	NULL},

	{"system.sw.arch",	0,		SYSTEM_SW_ARCH,		NULL},

	{"system.swap.size",	CF_HAVEPARAMS,	SYSTEM_SWAP_SIZE,	"all,free"},
	{"vm.vmemory.size",	CF_HAVEPARAMS,	VM_VMEMORY_SIZE,	"total"},

	{"system.uptime",	0,		SYSTEM_UPTIME,		NULL},

	{"system.uname",	0,		SYSTEM_UNAME,		NULL},

	{"service.discovery",	0,		SERVICE_DISCOVERY,	NULL},
	{"service.info",	CF_HAVEPARAMS,	SERVICE_INFO,		TREEGIX_SERVICE_NAME},
	{"service_state",	CF_HAVEPARAMS,	SERVICE_STATE,		TREEGIX_SERVICE_NAME},
	{"services",		CF_HAVEPARAMS,	SERVICES,		NULL},
	{"perf_counter",	CF_HAVEPARAMS,	PERF_COUNTER,		"\\System\\Processes"},
	{"perf_counter_en",	CF_HAVEPARAMS,	PERF_COUNTER_EN,	"\\System\\Processes"},
	{"proc_info",		CF_HAVEPARAMS,	PROC_INFO,		"svchost.exe"},

	{"__UserPerfCounter",	CF_HAVEPARAMS,	USER_PERF_COUNTER,	""},

	{"wmi.get",		CF_HAVEPARAMS,	WMI_GET,		"root\\cimv2,select Caption from Win32_OperatingSystem"},
	{"wmi.getall",		CF_HAVEPARAMS,	WMI_GETALL,		"root\\cimv2,select * from Win32_OperatingSystem"},

	{NULL}
};
