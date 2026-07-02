package main

import (
	"context"
	"flag"
	"io"
	"log"
	"net/http"
	"os"
	"os/signal"
	"runtime"
	"syscall"
	"time"

	"runbay/daemon/internal/api"
	"runbay/daemon/internal/logfiles"
	"runbay/daemon/internal/process"
	"runbay/daemon/internal/store"
)

func main() {
	addr := flag.String("addr", "127.0.0.1:8732", "HTTP listen address")
	dataPath := flag.String("data", defaultDataPath(), "task data file path")
	flag.Parse()

	if isWindowsService() {
		if err := runWindowsService("RunBay", *addr, *dataPath); err != nil {
			log.Fatalf("service failed: %v", err)
		}
		return
	}

	stop := make(chan struct{})
	sig := make(chan os.Signal, 1)
	signal.Notify(sig, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-sig
		close(stop)
	}()

	if err := runDaemon(*addr, *dataPath, stop); err != nil {
		log.Fatal(err)
	}
}

func runDaemon(addr, dataPath string, stop <-chan struct{}) error {
	logManager := logfiles.New(logfiles.DefaultRoot(), logfiles.DefaultRetentionDays)
	defer func() {
		if err := logManager.Close(); err != nil {
			log.Printf("failed to close log files: %v", err)
		}
	}()
	log.SetOutput(io.MultiWriter(os.Stderr, logManager.DaemonWriter()))
	logManager.StartCleanupLoop(stop)

	taskStore := store.NewMemoryStoreWithPathAndLogSink(dataPath, logManager)
	manager := process.NewManager(taskStore)
	server := api.NewServer(taskStore, manager)
	startLaunchTasks(taskStore, manager)

	httpServer := &http.Server{
		Addr:              addr,
		Handler:           server.Routes(),
		ReadHeaderTimeout: 5 * time.Second,
	}

	serverErr := make(chan error, 1)
	go func() {
		log.Printf("runbayd listening on http://%s", addr)
		log.Printf("task data: %s", dataPath)
		log.Printf("log data: %s", logfiles.DefaultRoot())
		if err := httpServer.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Printf("listen failed: %v", err)
			serverErr <- err
		}
	}()

	select {
	case <-stop:
	case err := <-serverErr:
		manager.StopAll()
		return err
	}

	log.Println("shutting down")
	manager.StopAll()

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := httpServer.Shutdown(ctx); err != nil {
		log.Printf("shutdown failed: %v", err)
		return err
	}
	return nil
}

func startLaunchTasks(taskStore *store.MemoryStore, manager *process.Manager) {
	for _, t := range taskStore.List() {
		if !t.StartOnLaunch {
			continue
		}
		if _, err := manager.Start(t.ID); err != nil {
			log.Printf("failed to start task %q on daemon launch: %v", t.Name, err)
		}
	}
}

func defaultDataPath() string {
	if value := os.Getenv("RUNBAY_DATA"); value != "" {
		return value
	}

	if runtime.GOOS == "windows" {
		if dir := os.Getenv("ProgramData"); dir != "" {
			return dir + string(os.PathSeparator) + "RunBay" + string(os.PathSeparator) + "tasks.json"
		}
		return `C:\ProgramData\RunBay\tasks.json`
	}

	return "/var/lib/runbay/tasks.json"
}
