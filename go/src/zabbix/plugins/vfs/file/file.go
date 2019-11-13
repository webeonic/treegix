/*
** Zabbix
** Copyright (C) 2001-2019 Zabbix SIA
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

package file

import (
	"errors"
	"time"
	"zabbix/internal/agent"
	"zabbix/pkg/plugin"
	"zabbix/pkg/std"
)

// Plugin -
type Plugin struct {
	plugin.Base
	timeout time.Duration
}

var impl Plugin

// Export -
func (p *Plugin) Export(key string, params []string, ctx plugin.ContextProvider) (result interface{}, err error) {
	switch key {
	case "vfs.file.cksum":
		return p.exportCksum(params)
	case "vfs.file.contents":
		return p.exportContents(params)
	case "vfs.file.exists":
		return p.exportExists(params)
	case "vfs.file.size":
		return p.exportSize(params)
	case "vfs.file.time":
		return p.exportTime(params)
	case "vfs.file.regexp":
		return p.exportRegexp(params)
	default:
		return nil, errors.New("Unsupported metric.")
	}
}

func (p *Plugin) Configure(options map[string]string) {
	p.timeout = time.Duration(agent.Options.Timeout) * time.Second
}

var stdOs std.Os

func init() {
	stdOs = std.NewOs()
	plugin.RegisterMetrics(&impl, "File",
		"vfs.file.cksum", "Returns File checksum, calculated by the UNIX cksum algorithm.",
		"vfs.file.contents", "Retrieves contents of the file.",
		"vfs.file.exists", "Returns if file exists or not.",
		"vfs.file.time", "Returns file time information.",
		"vfs.file.size", "Returns file size.",
		"vfs.file.regexp", "Find string in a file.")
}
