package textdecode

import "unicode/utf8"

func BytesToString(b []byte) string {
	if utf8.Valid(b) {
		return string(b)
	}
	return fallbackBytesToString(b)
}

