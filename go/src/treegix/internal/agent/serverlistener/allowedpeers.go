

package serverlistener

import (
	"net"
	"strings"
	"treegix/internal/agent"
)

// AllowedPeers is preparsed content of field Server
type AllowedPeers struct {
	ips   []net.IP
	nets  []*net.IPNet
	names []string
}

// GetAllowedPeers is parses the Server field
func GetAllowedPeers(options *agent.AgentOptions) (allowedPeers *AllowedPeers, err error) {
	ap := &AllowedPeers{}

	if options.Server != "" {
		opts := strings.Split(options.Server, ",")
		for _, o := range opts {
			peer := strings.Trim(o, " \t")
			if _, peerNet, err := net.ParseCIDR(peer); nil == err && !ap.isPresent(peerNet) {
				ap.nets = append(ap.nets, peerNet)
				maskLeadSize, maskTotalOnes := peerNet.Mask.Size()
				if 0 == maskLeadSize && 128 == maskTotalOnes {
					_, peerNet, _ = net.ParseCIDR("0.0.0.0/0")
					if !ap.isPresent(peerNet) {
						ap.nets = append(ap.nets, peerNet)
					}
				}
			} else if peerip := net.ParseIP(peer); nil != peerip && !ap.isPresent(peerip) {
				ap.ips = append(ap.ips, peerip)
			} else if !ap.isPresent(peer) {
				ap.names = append(ap.names, peer)
			}
		}
	}

	return ap, nil
}

// CheckPeer validate incoming connection peer
func (ap *AllowedPeers) CheckPeer(ip net.IP) bool {
	if ap.checkNetIP(ip) {
		return true
	}

	for _, nameAllowed := range ap.names {
		if ips, err := net.LookupHost(nameAllowed); nil == err {
			for _, ipPeer := range ips {
				ipAllowed := net.ParseIP(ipPeer)
				if ipAllowed.Equal(ip) {
					return true
				}
			}
		}
	}

	return false
}

func (ap *AllowedPeers) isPresent(value interface{}) bool {
	switch value.(type) {
	case *net.IPNet:
		for _, v := range ap.nets {
			if v.Contains(value.(*net.IPNet).IP) {
				return true
			}
		}
	case net.IP:
		if ap.checkNetIP(value.(net.IP)) {
			return true
		}
	case string:
		for _, v := range ap.names {
			if v == value {
				return true
			}
		}
	}

	return false
}

func (ap *AllowedPeers) checkNetIP(ip net.IP) bool {
	for _, netAllowed := range ap.nets {
		if netAllowed.Contains(ip) {
			return true
		}
	}
	for _, ipAllowed := range ap.ips {
		if ipAllowed.Equal(ip) {
			return true
		}
	}
	return false
}
