//{{NO_DEPENDENCIES}}
// Microsoft Developer Studio generated include file.
// Used by resource.rc
//
#ifndef _RESOURCE_H_
#define _RESOURCE_H_

#include "..\..\..\include\version.h"

#if defined(TREEGIX_AGENT)
#	include "treegix_agent_desc.h"
#elif defined(TREEGIX_GET)
#	include "treegix_get_desc.h"
#elif defined(TREEGIX_SENDER)
#	include "treegix_sender_desc.h"
#endif

#define VER_FILEVERSION		TREEGIX_VERSION_MAJOR,TREEGIX_VERSION_MINOR,TREEGIX_VERSION_PATCH,TREEGIX_VERSION_RC_NUM
#define VER_FILEVERSION_STR	TRX_STR(TREEGIX_VERSION_MAJOR) "." TRX_STR(TREEGIX_VERSION_MINOR) "." \
					TRX_STR(TREEGIX_VERSION_PATCH) "." TRX_STR(TREEGIX_VERSION_REVISION) "\0"
#define VER_PRODUCTVERSION	TREEGIX_VERSION_MAJOR,TREEGIX_VERSION_MINOR,TREEGIX_VERSION_PATCH
#define VER_PRODUCTVERSION_STR	TRX_STR(TREEGIX_VERSION_MAJOR) "." TRX_STR(TREEGIX_VERSION_MINOR) "." \
					TRX_STR(TREEGIX_VERSION_PATCH) TREEGIX_VERSION_RC "\0"
#define VER_COMPANYNAME_STR	"\0"
#define VER_LEGALCOPYRIGHT_STR	"" VER_COMPANYNAME_STR
#define VER_PRODUCTNAME_STR	"\0"

// Next default values for new objects
//
#ifdef APSTUDIO_INVOKED
#ifndef APSTUDIO_READONLY_SYMBOLS
#define _APS_NEXT_RESOURCE_VALUE	105
#define _APS_NEXT_COMMAND_VALUE		40001
#define _APS_NEXT_CONTROL_VALUE		1000
#define _APS_NEXT_SYMED_VALUE		101
#endif
#endif

#endif	/* _RESOURCE_H_ */
