

#include "common.h"
#include "sysinfo.h"

TRX_METRIC	parameters_specific[] =
/*	KEY			FLAG		FUNCTION		TEST PARAMETERS */
{
	{"vfs.fs.size",		CF_HAVEPARAMS,	VFS_FS_SIZE,		"/,free"},
	{"vfs.fs.inode",	CF_HAVEPARAMS,	VFS_FS_INODE,		"/,free"},
	{"vfs.fs.discovery",	0,		VFS_FS_DISCOVERY,	NULL},

	{"net.if.discovery",	0,		NET_IF_DISCOVERY,	NULL},
	{"net.if.in",		CF_HAVEPARAMS,	NET_IF_IN,		"lan0,bytes"},
	{"net.if.out",		CF_HAVEPARAMS,	NET_IF_OUT,		"lan0,bytes"},
	{"net.if.total",	CF_HAVEPARAMS,	NET_IF_TOTAL,		"lan0,bytes"},

	{"vm.memory.size",	CF_HAVEPARAMS,	VM_MEMORY_SIZE,		"free"},

	{"proc.num",		CF_HAVEPARAMS,  PROC_NUM,		"inetd"},

	{"system.cpu.util",	CF_HAVEPARAMS,	SYSTEM_CPU_UTIL,	"all,user,avg1"},
	{"system.cpu.load",	CF_HAVEPARAMS,	SYSTEM_CPU_LOAD,	"all,avg1"},
	{"system.cpu.num",	CF_HAVEPARAMS,	SYSTEM_CPU_NUM,		"online"},
	{"system.cpu.discovery",0,		SYSTEM_CPU_DISCOVERY,	NULL},

	{"system.uname",	0,		SYSTEM_UNAME,		NULL},
	{"system.sw.arch",	0,		SYSTEM_SW_ARCH,		NULL},

	{NULL}
};
