

package file

import (
	"reflect"
	"testing"
	"time"
	"treegix/pkg/std"
)

var CrcFile = "1234"

func TestFileCksum(t *testing.T) {
	stdOs = std.NewMockOs()

	impl.timeout = time.Second * 3

	stdOs.(std.MockOs).MockFile("text.txt", []byte(CrcFile))
	if result, err := impl.Export("vfs.file.cksum", []string{"text.txt"}, nil); err != nil {
		t.Errorf("vfs.file.cksum returned error %s", err.Error())
	} else {
		if crc, ok := result.(uint32); !ok {
			t.Errorf("vfs.file.cksum returned unexpected value type %s", reflect.TypeOf(result).Kind())
		} else {
			if crc != 3582362371 {
				t.Errorf("vfs.file.cksum returned invalid result")
			}
		}
	}
}
