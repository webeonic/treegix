

package serverlistener

import (
	"testing"
)

func TestFormatError(t *testing.T) {
	const notsupported = "TRX_NOTSUPPORTED"
	const message = "error message"
	pc := &passiveCheck{}
	result := pc.formatError(message)

	if string(result[:len(notsupported)]) != notsupported {
		t.Errorf("Expected error message to start with '%s' while got '%s'", notsupported,
			string(result[:len(notsupported)]))
		return
	}
	if result[len(notsupported)] != 0 {
		t.Errorf("Expected terminating zero after TRX_NOTSUPPORTED error prefix")
		return
	}

	if string(result[len(notsupported)+1:]) != message {
		t.Errorf("Expected error description '%s' while got '%s'", message, string(result[len(notsupported)+1:]))
		return
	}
}
