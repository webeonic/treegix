
package kernel

import (
	"bufio"
	"fmt"
	"strconv"
)

func getMax(proc bool) (max uint64, err error) {
	var fileName string

	if proc {
		fileName = "/proc/sys/kernel/pid_max"
	} else {
		fileName = "/proc/sys/fs/file-max"
	}

	file, err := stdOs.Open(fileName)
	if err == nil {
		var line []byte
		var long bool

		reader := bufio.NewReader(file)

		if line, long, err = reader.ReadLine(); err == nil && !long {
			max, err = strconv.ParseUint(string(line), 10, 64)
		}

		file.Close()
	}

	if err != nil {
		err = fmt.Errorf("Cannot obtain data from %s.", fileName)
	}

	return
}
