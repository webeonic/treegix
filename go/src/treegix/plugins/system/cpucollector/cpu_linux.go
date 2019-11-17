

package cpucollector

/*
#include <unistd.h>
*/
import "C"

import (
	"bufio"
	"bytes"
	"os"
	"strconv"
	"strings"
)

const (
	procStatLocation = "/proc/stat"
)

func (p *Plugin) collect() (err error) {
	var file *os.File
	if file, err = os.Open(procStatLocation); err != nil {
		return err
	}
	defer file.Close()

	var buf bytes.Buffer
	if _, err = buf.ReadFrom(file); err != nil {
		return
	}

	for _, cpu := range p.cpus {
		cpu.status = cpuStatusOffline
	}

	scanner := bufio.NewScanner(&buf)
	for scanner.Scan() {
		line := scanner.Text()
		if !strings.HasPrefix(line, "cpu") {
			continue
		}
		fields := strings.Fields(line)
		var index, status int
		if len(fields[0]) > 3 {
			var i int64
			if i, err = strconv.ParseInt(fields[0][3:], 10, 32); err != nil {
				return
			}
			if index = int(i); index < 0 || index+1 >= len(p.cpus) {
				p.Debugf("invalid CPU index %d", index)
				continue
			}

			status = cpuStatusOnline
		} else {
			index = -1
		}

		cpu := p.cpus[index+1]
		cpu.status = status

		slot := &cpu.history[cpu.tail]
		num := len(slot.counters)
		if num > len(fields)-1 {
			num = len(fields) - 1
		}
		for i := 0; i < num; i++ {
			slot.counters[i], _ = strconv.ParseUint(fields[i+1], 10, 64)
		}
		for i := num; i < len(slot.counters); i++ {
			slot.counters[i] = 0
		}
		// Linux includes guest times in user and nice times
		slot.counters[stateUser] -= slot.counters[stateGcpu]
		slot.counters[stateNice] -= slot.counters[stateGnice]

		if cpu.tail = cpu.tail.inc(); cpu.tail == cpu.head {
			cpu.head = cpu.head.inc()
		}
	}
	return nil
}

func (p *Plugin) numCPU() int {
	return int(C.sysconf(C._SC_NPROCESSORS_CONF))
}
