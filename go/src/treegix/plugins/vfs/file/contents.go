

package file

import (
	"bytes"
	"errors"
	"fmt"
)

func (p *Plugin) exportContents(params []string) (result interface{}, err error) {

	if len(params) != 1 && len(params) != 2 {
		return nil, errors.New("Wrong number of parameters")
	}

	var encoder string

	if len(params) == 2 {
		encoder = params[1]
	}

	f, err := stdOs.Stat(params[0])
	if err != nil {
		return nil, fmt.Errorf("Cannot obtain file %s information: %s", params[0], err)
	}
	filelen := f.Size()

	bnum := 64 * 1024
	if filelen > int64(bnum) {
		return nil, errors.New("File is too large for this check")
	}

	file, err := stdOs.Open(params[0])
	if err != nil {
		return nil, fmt.Errorf("Cannot open file %s: %s", params[0], err)
	}
	defer file.Close()

	buf := bytes.Buffer{}
	if _, err = buf.ReadFrom(file); err != nil {
		return nil, fmt.Errorf("Cannot read from file: %s", err)
	}

	outbuf := decode(encoder, buf.Bytes())

	return string(bytes.TrimRight(outbuf, "\n\r")), nil

}
