

package file

import (
	"reflect"
	"testing"
	"time"
	"treegix/pkg/std"
)

func TestFileContentsEncoding(t *testing.T) {
	stdOs = std.NewMockOs()

	impl.timeout = time.Second * 3

	stdOs.(std.MockOs).MockFile("text.txt", []byte{0xd0, 0xd2, 0xd3, 0xe3, 0xe1, 0xe2, 0xd0, 0x0d, 0x0a})
	if result, err := impl.Export("vfs.file.contents", []string{"text.txt", "iso-8859-5"}, nil); err != nil {
		t.Errorf("vfs.file.contents returned error %s", err.Error())
	} else {
		if contents, ok := result.(string); !ok {
			t.Errorf("vfs.file.contents returned unexpected value type %s", reflect.TypeOf(result).Kind())
		} else {
			if contents != "августа" {
				t.Errorf("vfs.file.contents returned invalid result")
			}
		}
	}
}

func TestFileContents(t *testing.T) {
	stdOs = std.NewMockOs()

	impl.timeout = time.Second * 3

	stdOs.(std.MockOs).MockFile("text.txt", []byte{208, 176, 208, 178, 208, 179, 209, 131, 209, 129, 209, 130, 208, 176, 13, 10})
	if result, err := impl.Export("vfs.file.contents", []string{"text.txt"}, nil); err != nil {
		t.Errorf("vfs.file.contents returned error %s", err.Error())
	} else {
		if contents, ok := result.(string); !ok {
			t.Errorf("vfs.file.contents returned unexpected value type %s", reflect.TypeOf(result).Kind())
		} else {
			if contents != "августа" {
				t.Errorf("vfs.file.contents returned invalid result")
			}
		}
	}
}
