

package plugin

import (
	"sync/atomic"
	"time"
)

const (
	DefaultCapacity = 100
)

// Collector - interface for periodical metric collection
type Collector interface {
	Collect() error
	Period() int
}

// Exporter - interface for exporting collected metrics
type Exporter interface {
	Export(key string, params []string, context ContextProvider) (interface{}, error)
}

// Runner - interface for managing background processes
type Runner interface {
	Start()
	Stop()
}

// Watcher - interface for fully custom monitoring
type Watcher interface {
	Watch(requests []*Request, context ContextProvider)
}

// Configurator - interface for plugin configuration in agent conf files
type Configurator interface {
	Configure(options map[string]string)
}

type ResultWriter interface {
	Write(result *Result)
	Flush()
	SlotsAvailable() int
	PersistSlotsAvailable() int
}

type Meta struct {
	lastLogsize uint64
	mtime       int32
	Data        interface{}
}

func (m *Meta) SetLastLogsize(value uint64) {
	atomic.StoreUint64(&m.lastLogsize, value)
}

func (m *Meta) LastLogsize() uint64 {
	return atomic.LoadUint64(&m.lastLogsize)
}

func (m *Meta) SetMtime(value int32) {
	atomic.StoreInt32(&m.mtime, value)
}

func (m *Meta) Mtime() int32 {
	return atomic.LoadInt32(&m.mtime)
}

type RegexpMatcher interface {
	Match(value string, pattern string, mode int, output_template *string) (match bool, output string)
}

type ContextProvider interface {
	ClientID() uint64
	ItemID() uint64
	Output() ResultWriter
	Meta() *Meta
	GlobalRegexp() RegexpMatcher
}

type Result struct {
	Itemid      uint64
	Value       *string
	Ts          time.Time
	Error       error
	LastLogsize *uint64
	Mtime       *int
	Persistent  bool
}

type Request struct {
	Itemid      uint64  `json:"itemid"`
	Key         string  `json:"key"`
	Delay       string  `json:"delay"`
	LastLogsize *uint64 `json:"lastlogsize"`
	Mtime       *int    `json:"mtime"`
}
