/*
** Treegix
** Copyright (C) 2001-2019 Treegix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

package plugins

import (
	_ "treegix/plugins/kernel"
	_ "treegix/plugins/log"
	_ "treegix/plugins/net/netif"
	_ "treegix/plugins/proc"
	_ "treegix/plugins/system/cpucollector"
	_ "treegix/plugins/system/uname"
	_ "treegix/plugins/system/uptime"
	_ "treegix/plugins/systemd"
	_ "treegix/plugins/systemrun"
	_ "treegix/plugins/vfs/dev"
	_ "treegix/plugins/vfs/file"
	_ "treegix/plugins/treegix/async"
	_ "treegix/plugins/treegix/stats"
	_ "treegix/plugins/treegix/sync"
)
