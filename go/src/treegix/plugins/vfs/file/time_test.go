

package file

import (
	"reflect"
	"testing"
	"time"
	"treegix/pkg/std"
)

func TestFileModifyTime(t *testing.T) {
	stdOs = std.NewMockOs()

	impl.timeout = time.Second * 3

	var filetime int64

	stdOs.(std.MockOs).MockFile("text.txt", []byte("1234"))
	if f, err := stdOs.Stat("text.txt"); err == nil {
		filetime = f.ModTime().Unix()
	} else {
		t.Errorf("vfs.file.time test returned error %s", err.Error())
	}
	if result, err := impl.Export("vfs.file.time", []string{"text.txt"}, nil); err != nil {
		t.Errorf("vfs.file.time returned error %s", err.Error())
	} else {
		if filemodtime, ok := result.(int64); !ok {
			t.Errorf("vfs.file.time returned unexpected value type %s", reflect.TypeOf(result).Kind())
		} else {
			if filemodtime != filetime {
				t.Errorf("vfs.file.time returned invalid result")
			}
		}
	}
}

func TestFileAccessTime(t *testing.T) {
	stdOs = std.NewMockOs()

	impl.timeout = time.Second * 3

	var filetime int64

	stdOs.(std.MockOs).MockFile("text.txt", []byte("1234"))
	if f, err := stdOs.Stat("text.txt"); err == nil {
		filetime = f.ModTime().Unix()
	} else {
		t.Errorf("vfs.file.time test returned error %s", err.Error())
	}
	if result, err := impl.Export("vfs.file.time", []string{"text.txt", "access"}, nil); err != nil {
		t.Errorf("vfs.file.time returned error %s", err.Error())
	} else {
		if filemodtime, ok := result.(int64); !ok {
			t.Errorf("vfs.file.time returned unexpected value type %s", reflect.TypeOf(result).Kind())
		} else {
			if filemodtime != filetime {
				t.Errorf("vfs.file.time returned invalid result")
			}
		}
	}
}

func TestFileChangeTime(t *testing.T) {
	stdOs = std.NewMockOs()

	impl.timeout = time.Second * 3

	var filetime int64

	stdOs.(std.MockOs).MockFile("text.txt", []byte("1234"))
	if f, err := stdOs.Stat("text.txt"); err == nil {
		filetime = f.ModTime().Unix()
	} else {
		t.Errorf("vfs.file.time test returned error %s", err.Error())
	}
	if result, err := impl.Export("vfs.file.time", []string{"text.txt", "change"}, nil); err != nil {
		t.Errorf("vfs.file.time returned error %s", err.Error())
	} else {
		if filemodtime, ok := result.(int64); !ok {
			t.Errorf("vfs.file.time returned unexpected value type %s", reflect.TypeOf(result).Kind())
		} else {
			if filemodtime != filetime {
				t.Errorf("vfs.file.time returned invalid result")
			}
		}
	}
}
