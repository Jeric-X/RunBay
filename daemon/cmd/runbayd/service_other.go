//go:build !windows

package main

func isWindowsService() bool {
	return false
}

func runWindowsService(name, addr, dataPath, logPath string) error {
	return nil
}
