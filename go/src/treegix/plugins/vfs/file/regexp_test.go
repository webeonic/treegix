

package file

import (
	"reflect"
	"regexp"
	"testing"
	"time"
	"treegix/pkg/std"
)

func TestExecuteRegex(t *testing.T) {
	type testCase struct {
		input   string
		pattern string
		output  string
		result  string
		match   bool
	}

	tests := []*testCase{
		&testCase{input: `1`, pattern: `1`, output: ``, result: `1`, match: true},
		&testCase{input: `1`, pattern: `2`, output: ``, result: `1`, match: false},
		&testCase{input: `123 456 789"`, pattern: `([0-9]+)`, output: `\1`, result: `123`, match: true},
		&testCase{input: `value ""`, pattern: `value "([^"]*)"`, output: `\1`, result: ``, match: true},
		&testCase{input: `b:xyz"`, pattern: `b:([^ ]+)`, output: `\\1`, result: `\1`, match: true},
		&testCase{input: `a:1 b:2`, pattern: `a:([^ ]+) b:([^ ]+)`, output: `\1,\2`, result: `1,2`, match: true},
		&testCase{input: `a:\2 b:xyz`, pattern: `a:([^ ]+) b:([^ ]+)`, output: `\1,\2`, result: `\2,xyz`, match: true},
		&testCase{input: `a value: 10 in text"`, pattern: `value: ([0-9]+)`, output: `\@`, result: `value: 10`, match: true},
		&testCase{input: `a value: 10 in text"`, pattern: `value: ([0-9]+)`, output: `\0`, result: `value: 10`, match: true},
		&testCase{input: `a:9 b:2`, pattern: `a:([^\d ]+) | b:([^ ]+)`, output: `\0,\1,\2`, result: ` b:2,,2`, match: true},
	}

	for _, c := range tests {
		t.Run(c.input, func(t *testing.T) {
			rx, _ := regexp.Compile(c.pattern)
			r, m := impl.executeRegex([]byte(c.input), rx, []byte(c.output))
			if !m && c.match {
				t.Errorf("expected match while returned false")
			}
			if m && !c.match {
				t.Errorf("expected not match while returned true")
			}
			if m && r != c.result {
				t.Errorf("expected match output '%s' while got '%s'", c.result, r)
			}
		})
	}
}

func TestFileRegexpOutput(t *testing.T) {
	stdOs = std.NewMockOs()

	impl.timeout = time.Second * 3

	stdOs.(std.MockOs).MockFile("text.txt", []byte{0xe4, 0xd5, 0xde, 0xe4, 0xd0, 0xdd, 0x0d, 0x0a})
	if result, err := impl.Export("vfs.file.regexp", []string{"text.txt", "(ф)", "iso-8859-5", "", "", "group 0: \\0 group 1: \\1 group 4: \\4"}, nil); err != nil {
		t.Errorf("vfs.file.regexp returned error %s", err.Error())
	} else {
		if contents, ok := result.(string); !ok {
			t.Errorf("vfs.file.regexp returned unexpected value type %s", reflect.TypeOf(result).Kind())
		} else {
			if contents != "group 0: ф group 1: ф group 4: " {
				t.Errorf("vfs.file.regexp returned invalid result")
			}
		}
	}
}

func TestFileRegexp(t *testing.T) {
	stdOs = std.NewMockOs()

	impl.timeout = time.Second * 3

	stdOs.(std.MockOs).MockFile("text.txt", []byte{0xd0, 0xd2, 0xd3, 0xe3, 0xe1, 0xe2, 0xd0, 0x0d, 0x0a})
	if result, err := impl.Export("vfs.file.regexp", []string{"text.txt", "(а)", "iso-8859-5", "", ""}, nil); err != nil {
		t.Errorf("vfs.file.regexp returned error %s", err.Error())
	} else {
		if contents, ok := result.(string); !ok {
			t.Errorf("vfs.file.regexp returned unexpected value type %s", reflect.TypeOf(result).Kind())
		} else {
			if contents != "августа" {
				t.Errorf("vfs.file.regexp returned invalid result")
			}
		}
	}
}
