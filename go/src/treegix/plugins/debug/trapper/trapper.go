

package trapper

import (
	"fmt"
	"io/ioutil"
	"net"
	"regexp"
	"strconv"
	"treegix/pkg/itemutil"
	"treegix/pkg/plugin"
	"treegix/pkg/watch"
)

// Plugin
type Plugin struct {
	plugin.Base
	manager   *watch.Manager
	listeners map[int]*trapListener
}

var impl Plugin

func (p *Plugin) Watch(requests []*plugin.Request, ctx plugin.ContextProvider) {
	p.manager.Lock()
	p.manager.Update(ctx.ClientID(), ctx.Output(), requests)
	p.manager.Unlock()
}

type trapListener struct {
	port     int
	listener net.Listener
	manager  *watch.Manager
}

func (t *trapListener) run() {
	for {
		conn, err := t.listener.Accept()
		if err != nil {
			if nerr, ok := err.(net.Error); ok && !nerr.Temporary() {
				break
			}
			continue
		}
		if b, err := ioutil.ReadAll(conn); err == nil {
			t.manager.Lock()
			t.manager.Notify(t, b)
			t.manager.Unlock()
		}
		conn.Close()
	}
}

func (t *trapListener) URI() (uri string) {
	return fmt.Sprintf("%d", t.port)
}

func (t *trapListener) Subscribe() (err error) {
	if t.listener, err = net.Listen("tcp", fmt.Sprintf(":%d", t.port)); err != nil {
		return
	}
	go t.run()
	return nil
}

func (t *trapListener) Unsubscribe() {
	t.listener.Close()
}

type trapFilter struct {
	pattern *regexp.Regexp
}

func (f *trapFilter) Convert(v interface{}) (value *string, err error) {
	if b, ok := v.([]byte); !ok {
		err = fmt.Errorf("unexpected traper conversion input type %T", v)
	} else {
		if f.pattern == nil || f.pattern.Match(b) {
			tmp := string(b)
			value = &tmp
		}
	}
	return
}

func (t *trapListener) NewFilter(key string) (filter watch.EventFilter, err error) {
	var params []string
	if _, params, err = itemutil.ParseKey(key); err != nil {
		return
	}
	var pattern *regexp.Regexp
	if len(params) > 1 {
		if pattern, err = regexp.Compile(params[1]); err != nil {
			return
		}
	}
	return &trapFilter{pattern: pattern}, nil
}

func (p *Plugin) EventSourceByURI(uri string) (es watch.EventSource, err error) {
	var port int
	if port, err = strconv.Atoi(uri); err != nil {
		return
	}
	var ok bool
	if es, ok = p.listeners[port]; !ok {
		err = fmt.Errorf(`not registered listener URI "%s"`, uri)
	}
	return
}

func (p *Plugin) EventSourceByKey(key string) (es watch.EventSource, err error) {
	var params []string
	if _, params, err = itemutil.ParseKey(key); err != nil {
		return
	}
	var port int
	if port, err = strconv.Atoi(params[0]); err != nil {
		return
	}
	var ok bool
	var listener *trapListener
	if listener, ok = p.listeners[port]; !ok {
		listener = &trapListener{port: port, manager: p.manager}
		p.listeners[port] = listener
	}
	return listener, nil
}

func init() {
	impl.manager = watch.NewManager(&impl)
	impl.listeners = make(map[int]*trapListener)

	plugin.RegisterMetrics(&impl, "DebugTrapper", "debug.trap", "Listen on port for incoming TCP data.")
}
