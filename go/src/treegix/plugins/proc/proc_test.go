
package proc

import (
	"syscall"
	"testing"
)

func BenchmarkRead2k(b *testing.B) {
	for i := 0; i < b.N; i++ {
		_, _ = read2k("/proc/self/stat")
	}
}

func BenchmarkReadAll(b *testing.B) {
	for i := 0; i < b.N; i++ {
		_, _ = readAll("/proc/self/stat")
	}
}

func BenchmarkSyscallRead(b *testing.B) {
	for i := 0; i < b.N; i++ {
		buffer := make([]byte, 2048)
		fd, err := syscall.Open("/proc/self/stat", syscall.O_RDONLY, 0)
		if err != nil {
			return
		}

		syscall.Read(fd, buffer)
		syscall.Close(fd)
	}
}
