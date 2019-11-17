

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
