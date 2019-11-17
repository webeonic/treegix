

package serverconnector

import (
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"time"
	"treegix/pkg/tls"
	"treegix/pkg/zbxcomms"
)

type activeConnection struct {
	address   string
	localAddr net.Addr
	tlsConfig *tls.Config
}

func (c *activeConnection) Write(data []byte, timeout time.Duration) (err error) {
	b, err := zbxcomms.Exchange(c.address, &c.localAddr, timeout, data, c.tlsConfig)
	if err != nil {
		return err
	}

	var response agentDataResponse

	err = json.Unmarshal(b, &response)
	if err != nil {
		return err
	}

	if response.Response != "success" {
		if len(response.Info) != 0 {
			return fmt.Errorf("%s", response.Info)
		}
		return errors.New("unsuccessful response")
	}

	return nil
}

func (c *activeConnection) Addr() (s string) {
	return c.address
}

func (c *activeConnection) CanRetry() (enabled bool) {
	return true
}
