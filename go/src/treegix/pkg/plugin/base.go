

package plugin

import (
	"fmt"
	"treegix/pkg/log"
)

type Accessor interface {
	Init(name string)
	Name() string
	Capacity() int
	SetCapacity(capactity int)
}

type Base struct {
	name     string
	capacity int
}

func (b *Base) Init(name string) {
	b.name = name
	b.capacity = DefaultCapacity
}

func (b *Base) Name() string {
	return b.name
}

func (b *Base) Capacity() int {
	return b.capacity
}

func (b *Base) SetCapacity(capacity int) {
	b.capacity = capacity
}

func (b *Base) Tracef(format string, args ...interface{}) {
	log.Tracef("[%s] %s", b.name, fmt.Sprintf(format, args...))
}

func (b *Base) Debugf(format string, args ...interface{}) {
	log.Debugf("[%s] %s", b.name, fmt.Sprintf(format, args...))
}

func (b *Base) Warningf(format string, args ...interface{}) {
	log.Warningf("[%s] %s", b.name, fmt.Sprintf(format, args...))
}

func (b *Base) Infof(format string, args ...interface{}) {
	log.Infof("[%s] %s", b.name, fmt.Sprintf(format, args...))
}

func (b *Base) Errf(format string, args ...interface{}) {
	log.Errf("[%s] %s", b.name, fmt.Sprintf(format, args...))
}

func (b *Base) Critf(format string, args ...interface{}) {
	log.Critf("[%s] %s", b.name, fmt.Sprintf(format, args...))
}
