

package file

import (
	"errors"
	"fmt"
)

// Export -
func (p *Plugin) exportSize(params []string) (result interface{}, err error) {
	if len(params) != 1 {
		return nil, errors.New("Invalid number of parameters.")
	}
	if "" == params[0] {
		return nil, errors.New("Invalid first parameter.")
	}

	if f, err := stdOs.Stat(params[0]); err == nil {
		return f.Size(), nil
	} else {
		return nil, fmt.Errorf("Cannot obtain file information: %s", err)
	}
}
