

// Package version provides treegix release version
package version

import (
	"fmt"
	"strings"
	"treegix/pkg/tls"
)

const (
	APPLICATION_NAME        = "Treegix Agent"
	TREEGIX_REVDATE          = "28 October 2019"
	TREEGIX_VERSION_MAJOR    = 4
	TREEGIX_VERSION_MINOR    = 4
	TREEGIX_VERSION_PATCH    = 1
	TREEGIX_VERSION_RC       = ""
	TREEGIX_VERSION_RC_NUM   = "1400"
	TREEGIX_VERSION_REVISION = "8870606e6a"
	copyrightMessage        = ""
)

var (
	titleMessage string = "{undefined}"
	compileDate  string = "{undefined}"
	compileTime  string = "{undefined}"
	compileOs    string = "{undefined}"
	compileArch  string = "{undefined}"
	compileMode  string
)

func ApplicationName() string {
	return APPLICATION_NAME
}
func RevDate() string {
	return TREEGIX_REVDATE
}

func Major() int {
	return TREEGIX_VERSION_MAJOR
}

func Minor() int {
	return TREEGIX_VERSION_MINOR
}

func Patch() int {
	return TREEGIX_VERSION_PATCH
}

func RC() string {
	return TREEGIX_VERSION_RC
}

func LongStr() string {
	var ver string = fmt.Sprintf("%d.%d.%d", Major(), Minor(), Patch())
	if len(RC()) != 0 {
		ver += " " + RC()
	}
	return ver
}

func Long() string {
	var ver string = fmt.Sprintf("%d.%d.%d", Major(), Minor(), Patch())
	if len(RC()) != 0 {
		ver += RC()
	}
	return ver
}

func Short() string {
	return fmt.Sprintf("%d.%d", Major(), Minor())
}

func Revision() string {
	return TREEGIX_VERSION_REVISION
}

func CopyrightMessage() string {
	return copyrightMessage + tls.CopyrightMessage()
}

func CompileDate() string {
	return compileDate
}

func CompileTime() string {
	return compileTime
}

func CompileOs() string {
	return compileOs
}

func CompileArch() string {
	return compileArch
}

func CompileMode() string {
	return compileMode
}

func TitleMessage() string {
	var title string = titleMessage
	if "windows" == compileOs {
		if -1 < strings.Index(compileArch, "64") {
			title += " Win64"
		} else {
			title += " Win32"
		}
	}

	if len(compileMode) != 0 {
		title += fmt.Sprintf(" (%s)", compileMode)
	}

	return title
}

func Display() {
	fmt.Printf("%s (Treegix) %s\n", TitleMessage(), LongStr())
	fmt.Printf("Revision %s %s, compilation time: %s %s\n\n", Revision(), RevDate(), CompileDate(), CompileTime())
	fmt.Println(CopyrightMessage())
}
