

package agent

import "sync/atomic"

var lastClientID uint64

// Internal client id assigned to each active server and unique passive bulk request.
// Single checks (internal and old style passive checks) has built-in client id 0.
func NewClientID() uint64 {
	return atomic.AddUint64(&lastClientID, 1)
}
