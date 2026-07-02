//go:build !windows

package textdecode

func fallbackBytesToString(b []byte) string {
	return string(b)
}

