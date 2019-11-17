

package uname

import (
	"fmt"
	"syscall"
)

func arrayToString(unameArray *[65]int8) string {
	var byteString [65]byte
	var indexLength int
	for ; indexLength < len(unameArray); indexLength++ {
		if 0 == unameArray[indexLength] {
			break
		}
		byteString[indexLength] = uint8(unameArray[indexLength])
	}
	return string(byteString[:indexLength])
}

func getUname() (uname string, err error) {
	var utsname syscall.Utsname
	if err = syscall.Uname(&utsname); err != nil {
		err = fmt.Errorf("Cannot obtain system information: %s", err.Error())
		return
	}
	uname = fmt.Sprintf("%s %s %s %s %s", arrayToString(&utsname.Sysname), arrayToString(&utsname.Nodename),
		arrayToString(&utsname.Release), arrayToString(&utsname.Version), arrayToString(&utsname.Machine))

	return uname, nil
}

func getHostname() (hostname string, err error) {
	var utsname syscall.Utsname
	if err = syscall.Uname(&utsname); err != nil {
		err = fmt.Errorf("Cannot obtain system information: %s", err.Error())
		return
	}

	return arrayToString(&utsname.Nodename), nil
}

func getSwArch() (uname string, err error) {
	var utsname syscall.Utsname
	if err = syscall.Uname(&utsname); err != nil {
		err = fmt.Errorf("Cannot obtain system information: %s", err.Error())
		return
	}

	return arrayToString(&utsname.Machine), nil
}
