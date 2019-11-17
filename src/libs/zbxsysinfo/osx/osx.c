

#include "common.h"
#include "sysinfo.h"

TRX_METRIC	parameters_specific[] =
/*	KEY			FLAG		FUNCTION		TEST PARAMETERS */
{
	{"kernel.maxfiles",	0,		KERNEL_MAXFILES,	NULL},
	{"kernel.maxproc",	0,		KERNEL_MAXPROC,	NULL},

	{"vfs.fs.size",		CF_HAVEPARAMS,	VFS_FS_SIZE,		"/,free"},
	{"vfs.fs.inode",	CF_HAVEPARAMS,	VFS_FS_INODE,		"/,free"},
	{"vfs.fs.discovery",	0,		VFS_FS_DISCOVERY,	NULL},

	{"vm.memory.size",	CF_HAVEPARAMS,	VM_MEMORY_SIZE,		"free"},

	{"net.tcp.listen",	CF_HAVEPARAMS,	NET_TCP_LISTEN, 	"80"},
	{"net.udp.listen",	CF_HAVEPARAMS,	NET_UDP_LISTEN, 	"68"},

	{"net.if.in",		CF_HAVEPARAMS,	NET_IF_IN,		"en0,bytes"},
	{"net.if.out",		CF_HAVEPARAMS,	NET_IF_OUT,		"en0,bytes"},
	{"net.if.total",	CF_HAVEPARAMS,	NET_IF_TOTAL,		"en0,bytes"},
	{"net.if.collisions",   CF_HAVEPARAMS,	NET_IF_COLLISIONS,      "en0"},

	{"system.cpu.num",	CF_HAVEPARAMS,	SYSTEM_CPU_NUM,		"online"},
	{"system.cpu.load",	CF_HAVEPARAMS,	SYSTEM_CPU_LOAD,	"all,avg1"},
	{"system.cpu.discovery",0,		SYSTEM_CPU_DISCOVERY,	NULL},

	{"system.uname",	0,		SYSTEM_UNAME,		NULL},

	{"system.uptime",	0,		SYSTEM_UPTIME,		NULL},
	{"system.boottime",	0,		SYSTEM_BOOTTIME,	NULL},
	{"system.sw.arch",	0,		SYSTEM_SW_ARCH,		NULL},

	{NULL}
};
