

package file

import (
	"reflect"
	"testing"
	"time"
	"treegix/pkg/std"
)

func TestFileExists(t *testing.T) {
	stdOs = std.NewMockOs()

	impl.timeout = time.Second * 3

	stdOs.(std.MockOs).MockFile("text.txt", []byte("1234"))
	if result, err := impl.Export("vfs.file.exists", []string{"text.txt"}, nil); err != nil {
		t.Errorf("vfs.file.exists returned error %s", err.Error())
	} else {
		if exists, ok := result.(int); !ok {
			t.Errorf("vfs.file.exists returned unexpected value type %s", reflect.TypeOf(result).Kind())
		} else {
			if exists != 1 {
				t.Errorf("vfs.file.exists returned invalid result")
			}
		}
	}
}

func TestFileNotExists(t *testing.T) {
	stdOs = std.NewMockOs()

	impl.timeout = time.Second * 3

	stdOs.(std.MockOs).MockFile("text.txt", []byte("1234"))
	if result, err := impl.Export("vfs.file.exists", []string{"text2.txt"}, nil); err != nil {
		t.Errorf("vfs.file.exists returned error %s", err.Error())
	} else {
		if exists, ok := result.(int); !ok {
			t.Errorf("vfs.file.exists returned unexpected value type %s", reflect.TypeOf(result).Kind())
		} else {
			if exists != 0 {
				t.Errorf("vfs.file.exists returned invalid result")
			}
		}
	}
}
