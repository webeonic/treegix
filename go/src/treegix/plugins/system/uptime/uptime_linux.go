

package uptime

import (
	"bufio"
	"errors"
	"fmt"
	"treegix/pkg/std"
	"strconv"
	"strings"
	"time"
)

func getUptime() (uptime int, err error) {
	var file std.File
	if file, err = stdOs.Open("/proc/stat"); err != nil {
		err = fmt.Errorf("Cannot read boot time: %s", err.Error())
		return
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		if strings.HasPrefix(scanner.Text(), "btime") {
			var boot int
			if boot, err = strconv.Atoi(scanner.Text()[6:]); err != nil {
				return
			}
			return int(time.Now().Unix()) - boot, nil
		}
	}

	return 0, errors.New("Cannot locate boot time")
}
