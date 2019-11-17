

package monitor

import (
	"sync"
)

var waitGroup sync.WaitGroup

// ServiceStarted must be called by internal services at start
func Register() {
	waitGroup.Add(1)
}

// ServiceStopped must be called by internal services at exit
func Unregister() {
	waitGroup.Done()
}

// WaitForServices waits until all started services are stopped
func Wait() {
	waitGroup.Wait()
}
