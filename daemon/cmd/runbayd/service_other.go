//go:build !windows

package main

func isWindowsService() bool {
	return false
}

func runWindowsService(name, addr, dataPath string) error {
	return nil
}
