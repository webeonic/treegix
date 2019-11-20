

package trxcmd

import (
	"bytes"
	"fmt"
	"os/exec"
	"strings"
	"syscall"
	"time"
	"treegix/pkg/log"
)

const maxExecuteOutputLenB = 512 * 1024

func Execute(s string, timeout time.Duration) (string, error) {
	cmd := exec.Command("sh", "-c", s)

	var b bytes.Buffer
	cmd.Stdout = &b
	cmd.Stderr = &b

	cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}

	err := cmd.Start()

	if err != nil {
		return "", fmt.Errorf("Cannot execute command: %s", err)
	}

	t := time.AfterFunc(timeout, func() {
		errKill := syscall.Kill(-cmd.Process.Pid, syscall.SIGTERM)
		if errKill != nil {
			log.Warningf("failed to kill [%s]: %s", s, errKill)
		}
	})

	cmd.Wait()

	if !t.Stop() {
		return "", fmt.Errorf("Timeout while executing a shell script.")
	}

	if maxExecuteOutputLenB <= len(b.String()) {
		return "", fmt.Errorf("Command output exceeded limit of %d KB", maxExecuteOutputLenB/1024)
	}

	return strings.TrimRight(b.String(), " \t\r\n"), nil
}

func ExecuteBackground(s string) error {
	cmd := exec.Command("sh", "-c", s)
	err := cmd.Start()

	if err != nil {
		return fmt.Errorf("Cannot execute command: %s", err)
	}

	go cmd.Wait()

	return nil
}
