//go:build windows

package process

import "os/exec"

func killProcessTree(cmd *exec.Cmd) error {
	if cmd.Process == nil {
		return nil
	}

	kill := exec.Command("taskkill", "/PID", itoa(cmd.Process.Pid), "/T", "/F")
	if err := kill.Run(); err != nil {
		return cmd.Process.Kill()
	}
	return nil
}

func itoa(value int) string {
	if value == 0 {
		return "0"
	}

	var buf [20]byte
	i := len(buf)
	n := value
	for n > 0 {
		i--
		buf[i] = byte('0' + n%10)
		n /= 10
	}
	return string(buf[i:])
}

