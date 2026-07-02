//go:build windows

package textdecode

import (
	"syscall"
	"unsafe"
)

const (
	cpACP   = 0
	cpOEMCP = 1
)

var (
	kernel32                 = syscall.NewLazyDLL("kernel32.dll")
	procMultiByteToWideChar = kernel32.NewProc("MultiByteToWideChar")
)

func fallbackBytesToString(b []byte) string {
	if s, ok := decodeCodePage(cpOEMCP, b); ok {
		return s
	}
	if s, ok := decodeCodePage(cpACP, b); ok {
		return s
	}
	return string(b)
}

func decodeCodePage(codePage uint32, b []byte) (string, bool) {
	if len(b) == 0 {
		return "", true
	}

	size, _, _ := procMultiByteToWideChar.Call(
		uintptr(codePage),
		0,
		uintptr(unsafe.Pointer(&b[0])),
		uintptr(len(b)),
		0,
		0,
	)
	if size == 0 {
		return "", false
	}

	wide := make([]uint16, size)
	written, _, _ := procMultiByteToWideChar.Call(
		uintptr(codePage),
		0,
		uintptr(unsafe.Pointer(&b[0])),
		uintptr(len(b)),
		uintptr(unsafe.Pointer(&wide[0])),
		size,
	)
	if written == 0 {
		return "", false
	}
	return syscall.UTF16ToString(wide), true
}

