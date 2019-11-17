

package file

import (
	"reflect"
	"testing"
	"time"
	"treegix/pkg/std"
)

func TestFileSize(t *testing.T) {
	stdOs = std.NewMockOs()

	impl.timeout = time.Second * 3

	stdOs.(std.MockOs).MockFile("text.txt", []byte("1234"))
	if result, err := impl.Export("vfs.file.size", []string{"text.txt"}, nil); err != nil {
		t.Errorf("vfs.file.size returned error %s", err.Error())
	} else {
		if filesize, ok := result.(int64); !ok {
			t.Errorf("vfs.file.size returned unexpected value type %s", reflect.TypeOf(result).Kind())
		} else {
			if filesize != 4 {
				t.Errorf("vfs.file.size returned invalid result")
			}
		}
	}
}
