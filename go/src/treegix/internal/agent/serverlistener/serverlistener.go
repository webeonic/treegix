

package serverlistener

import (
	"fmt"
	"net"
	"strings"
	"time"
	"treegix/internal/agent"
	"treegix/internal/agent/scheduler"
	"treegix/internal/monitor"
	"treegix/pkg/log"
	"treegix/pkg/tls"
	"treegix/pkg/trxcomms"
)

type ServerListener struct {
	listenerID   int
	listener     *trxcomms.Listener
	scheduler    scheduler.Scheduler
	options      *agent.AgentOptions
	tlsConfig    *tls.Config
	allowedPeers *AllowedPeers
	bindIP       string
}

func (sl *ServerListener) processConnection(conn *trxcomms.Connection) (err error) {
	defer func() {
		if err != nil {
			conn.Close()
		}
	}()

	var data []byte
	if data, err = conn.Read(time.Second * time.Duration(sl.options.Timeout)); err != nil {
		return
	}

	log.Debugf("received passive check request: '%s' from '%s'", string(data), conn.RemoteIP())

	response := passiveCheck{conn: &passiveConnection{conn: conn}, scheduler: sl.scheduler}
	go response.handleCheck(data)

	return nil
}

func (sl *ServerListener) run() {
	defer log.PanicHook()
	log.Debugf("[%d] starting listener for '%s:%d'", sl.listenerID, sl.bindIP, sl.options.ListenPort)

	for {
		conn, err := sl.listener.Accept()
		if err == nil {
			if !sl.allowedPeers.CheckPeer(net.ParseIP(conn.RemoteIP())) {
				conn.Close()
				log.Warningf("cannot accept incoming connection for peer: %s", conn.RemoteIP())
			} else if err := sl.processConnection(conn); err != nil {
				log.Warningf("cannot process incoming connection: %s", err.Error())
			}
		} else {
			if nerr, ok := err.(net.Error); ok && nerr.Temporary() {
				log.Errf("cannot accept incoming connection: %s", err.Error())
				continue
			}
			break
		}
	}

	log.Debugf("listener has been stopped")
	monitor.Unregister()

}

func New(listenerID int, s scheduler.Scheduler, bindIP string, options *agent.AgentOptions) (sl *ServerListener) {
	sl = &ServerListener{listenerID: listenerID, scheduler: s, bindIP: bindIP, options: options}
	return
}

func (sl *ServerListener) Start() (err error) {
	if sl.tlsConfig, err = agent.GetTLSConfig(sl.options); err != nil {
		return
	}
	if sl.allowedPeers, err = GetAllowedPeers(sl.options); err != nil {
		return
	}
	if sl.listener, err = trxcomms.Listen(fmt.Sprintf("%s:%d", sl.bindIP, sl.options.ListenPort), sl.tlsConfig); err != nil {
		return
	}
	monitor.Register()
	go sl.run()
	return
}

func (sl *ServerListener) Stop() {
	if sl.listener != nil {
		sl.listener.Close()
	}
}

// ParseListenIP validate ListenIP value
func ParseListenIP(options *agent.AgentOptions) (ips []string, err error) {
	if 0 == len(options.ListenIP) {
		return []string{"0.0.0.0"}, nil
	}
	lips := getListLocalIP()
	opts := strings.Split(options.ListenIP, ",")
	for _, o := range opts {
		addr := strings.Trim(o, " \t")
		if err = validateLocalIP(addr, lips); nil != err {
			return nil, err
		}
		ips = append(ips, addr)
	}
	return ips, nil
}

func validateLocalIP(addr string, lips *[]net.IP) (err error) {
	if ip := net.ParseIP(addr); nil != ip {
		if ip.IsLoopback() || 0 == len(*lips) {
			return nil
		}
		for _, lip := range *lips {
			if lip.Equal(ip) {
				return nil
			}
		}
	} else {
		return fmt.Errorf("incorrect value of ListenIP: \"%s\"", addr)
	}
	return fmt.Errorf("value of ListenIP not present on the host: \"%s\"", addr)
}

func getListLocalIP() *[]net.IP {
	var ips []net.IP

	ifaces, err := net.Interfaces()
	if nil != err {
		return &ips
	}

	for _, i := range ifaces {
		addrs, err := i.Addrs()
		if nil != err {
			return &ips
		}

		for _, addr := range addrs {
			var ip net.IP
			switch v := addr.(type) {
			case *net.IPNet:
				ip = v.IP
			case *net.IPAddr:
				ip = v.IP
			}
			ips = append(ips, ip)
		}
	}

	return &ips
}
