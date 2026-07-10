//go:build windows

package main

import (
	"log"

	"golang.org/x/sys/windows/svc"
)

type runbayService struct {
	addr     string
	dataPath string
	logPath  string
}

func isWindowsService() bool {
	isService, err := svc.IsWindowsService()
	return err == nil && isService
}

func runWindowsService(name, addr, dataPath, logPath string) error {
	return svc.Run(name, &runbayService{addr: addr, dataPath: dataPath, logPath: logPath})
}

func (s *runbayService) Execute(args []string, requests <-chan svc.ChangeRequest, changes chan<- svc.Status) (bool, uint32) {
	_ = args

	const accepted = svc.AcceptStop | svc.AcceptShutdown
	stop := make(chan struct{})
	done := make(chan error, 1)

	changes <- svc.Status{State: svc.StartPending}
	go func() {
		done <- runDaemon(s.addr, s.dataPath, s.logPath, stop)
	}()
	changes <- svc.Status{State: svc.Running, Accepts: accepted}

	for {
		select {
		case request := <-requests:
			switch request.Cmd {
			case svc.Interrogate:
				changes <- request.CurrentStatus
			case svc.Stop, svc.Shutdown:
				changes <- svc.Status{State: svc.StopPending}
				close(stop)
				if err := <-done; err != nil {
					log.Printf("daemon stopped with error: %v", err)
				}
				changes <- svc.Status{State: svc.Stopped}
				return false, 0
			default:
			}
		case err := <-done:
			if err != nil {
				log.Printf("daemon exited with error: %v", err)
				return false, 1
			}
			return false, 0
		}
	}
}
