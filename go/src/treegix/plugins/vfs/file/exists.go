

package file

import (
	"errors"
)

// Export -
func (p *Plugin) exportExists(params []string) (result interface{}, err error) {
	if len(params) > 1 {
		return nil, errors.New("Too many parameters.")
	}
	if len(params) == 0 || params[0] == "" {
		return nil, errors.New("Invalid first parameter.")
	}

	ret := 0

	if f, err := stdOs.Stat(params[0]); err == nil {
		if mode := f.Mode(); mode.IsRegular() {
			ret = 1
		}
	}
	return ret, nil
}
