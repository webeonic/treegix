

package file

import (
	"errors"
	"fmt"
	"syscall"
)

// Export -
func (p *Plugin) exportTime(params []string) (result interface{}, err error) {
	if len(params) > 2 || len(params) == 0 {
		return nil, errors.New("Invalid number of parameters.")
	}
	if "" == params[0] {
		return nil, errors.New("Invalid first parameter.")
	}
	if f, err := stdOs.Stat(params[0]); err != nil {
		return nil, fmt.Errorf("Cannot obtain file information: %s", err)
	} else {
		if len(params) == 1 || params[1] == "" || params[1] == "modify" {
			return f.ModTime().Unix(), nil
		} else if params[1] == "access" {
			return f.Sys().(*syscall.Stat_t).Atim.Sec, nil
		} else if params[1] == "change" {
			return f.Sys().(*syscall.Stat_t).Ctim.Sec, nil
		} else {
			return nil, errors.New("Invalid second parameter.")
		}
	}
}
