

#include "common.h"
#include "sysinfo.h"

TRX_METRIC	parameters_specific[] =
/*	KEY			FLAG		FUNCTION		TEST PARAMETERS */
{
	{"kernel.maxfiles",	0,		KERNEL_MAXFILES,	NULL},
	{"kernel.maxproc",	0,		KERNEL_MAXPROC,		NULL},

	{"vfs.fs.size",		CF_HAVEPARAMS,	VFS_FS_SIZE,		"/"},
	{"vfs.fs.inode",	CF_HAVEPARAMS,	VFS_FS_INODE,		"/,free"},
	{"vfs.fs.discovery",	0,		VFS_FS_DISCOVERY,	NULL},

	{"net.if.in",		CF_HAVEPARAMS,	NET_IF_IN,		"lo0,bytes"},
	{"net.if.out",		CF_HAVEPARAMS,	NET_IF_OUT,		"lo0,bytes"},
	{"net.if.total",	CF_HAVEPARAMS,	NET_IF_TOTAL,		"lo0,bytes"},
	{"net.if.collisions",	CF_HAVEPARAMS,	NET_IF_COLLISIONS,	"lo0"},
	{"net.if.discovery",	0,		NET_IF_DISCOVERY,	"lo0"},

	{"vm.memory.size",	CF_HAVEPARAMS,	VM_MEMORY_SIZE,		"free"},

	{"proc.num",		CF_HAVEPARAMS,	PROC_NUM,		"inetd"},
	{"proc.mem",		CF_HAVEPARAMS,	PROC_MEM,		"inetd"},

	{"system.cpu.switches",	0,		SYSTEM_CPU_SWITCHES,	NULL},
	{"system.cpu.intr",	0,		SYSTEM_CPU_INTR,	NULL},
	{"system.cpu.util",	CF_HAVEPARAMS,	SYSTEM_CPU_UTIL,	"all,user,avg1"},
	{"system.cpu.load",	CF_HAVEPARAMS,	SYSTEM_CPU_LOAD,	"all,avg1"},
	{"system.cpu.num",	CF_HAVEPARAMS,	SYSTEM_CPU_NUM,		"online"},
	{"system.cpu.discovery",0,		SYSTEM_CPU_DISCOVERY,	NULL},

	{"system.uname",	0,		SYSTEM_UNAME,		NULL},

	{"system.uptime",	0,		SYSTEM_UPTIME,		NULL},
	{"system.boottime",	0,		SYSTEM_BOOTTIME,	NULL},
	{"system.sw.arch",	0,		SYSTEM_SW_ARCH,		NULL},

	{NULL}
};
