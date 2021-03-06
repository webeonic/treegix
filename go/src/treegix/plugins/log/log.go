
package log

import (
	"fmt"
	"runtime"
	"time"
	"unsafe"
	"treegix/internal/agent"
	"treegix/pkg/glexpr"
	"treegix/pkg/itemutil"
	"treegix/pkg/plugin"
	"treegix/pkg/trxlib"
)

// Plugin -
type Plugin struct {
	plugin.Base
}

func (p *Plugin) Configure(options map[string]string) {
	trxlib.SetMaxLinesPerSecond(agent.Options.MaxLinesPerSecond)
}

type metadata struct {
	key       string
	params    []string
	blob      unsafe.Pointer
	lastcheck time.Time
}

func (p *Plugin) Export(key string, params []string, ctx plugin.ContextProvider) (result interface{}, err error) {
	if ctx == nil || ctx.ClientID() == 0 {
		return nil, fmt.Errorf(`The "%s" key is not supported in test or single passive check mode`, key)
	}
	meta := ctx.Meta()
	var data *metadata
	if meta.Data == nil {
		data = &metadata{key: key, params: params}
		meta.Data = data
		runtime.SetFinalizer(data, func(d *metadata) { trxlib.FreeActiveMetric(d.blob) })
		if data.blob, err = trxlib.NewActiveMetric(key, params, meta.LastLogsize(), meta.Mtime()); err != nil {
			return nil, err
		}
	} else {
		data = meta.Data.(*metadata)
		if !itemutil.CompareKeysParams(key, params, data.key, data.params) {
			p.Debugf("item %d key has been changed, resetting log metadata", ctx.ItemID())
			trxlib.FreeActiveMetric(data.blob)
			data.key = key
			data.params = params
			// reset lastlogsize/mtime if item key has been changed
			if data.blob, err = trxlib.NewActiveMetric(key, params, 0, 0); err != nil {
				return nil, err
			}
		}
	}

	if ctx.Output().PersistSlotsAvailable() == 0 {
		p.Warningf("buffer is full, cannot store persistent value")
		return nil, nil
	}

	// with flexible checks there are no guaranteed refresh time,
	// so using number of seconds elapsed since last check
	now := time.Now()
	var refresh int
	if data.lastcheck.IsZero() {
		refresh = 1
	} else {
		refresh = int((now.Sub(data.lastcheck) + time.Second/2) / time.Second)
	}
	logitem := trxlib.LogItem{Results: make([]*trxlib.LogResult, 0), Output: ctx.Output()}
	grxp := ctx.GlobalRegexp().(*glexpr.Bundle)
	trxlib.ProcessLogCheck(data.blob, &logitem, refresh, grxp.Cblob)
	data.lastcheck = now

	if len(logitem.Results) != 0 {
		results := make([]plugin.Result, len(logitem.Results))
		for i, r := range logitem.Results {
			results[i].Itemid = ctx.ItemID()
			results[i].Value = r.Value
			results[i].Error = r.Error
			results[i].Ts = r.Ts
			results[i].LastLogsize = &r.LastLogsize
			results[i].Mtime = &r.Mtime
			results[i].Persistent = true
		}
		return results, nil
	}
	return nil, nil
}

var impl Plugin

func init() {
	plugin.RegisterMetrics(&impl, "Log",
		"log", "Log file monitoring.",
		"logrt", "Log file monitoring with log rotation support.",
		"log.count", "Count of matched lines in log file monitoring.",
		"logrt.count", "Count of matched lines in log file monitoring with log rotation support.")
}
