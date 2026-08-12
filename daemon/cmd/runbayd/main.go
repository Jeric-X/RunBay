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
	"runbay/daemon/internal/serviceconfig"
	"runbay/daemon/internal/store"
)

func main() {
	addr := flag.String("addr", "127.0.0.1:8732", "HTTP listen address")
	dataPath := flag.String("data", defaultDataPath(), "task data file path")
	logPath := flag.String("log-dir", logfiles.DefaultRoot(), "log directory path")
	serviceConfigPath := flag.String("service-config", "", "platform-neutral daemon service configuration file")
	flag.Parse()

	if *serviceConfigPath != "" {
		config, err := serviceconfig.Load(*serviceConfigPath)
		if err != nil {
			log.Fatalf("load service config: %v", err)
		}
		*addr = config.ListenAddress
		*dataPath = config.DataFile
		*logPath = config.LogDirectory
	}

	if isWindowsService() {
		if err := runWindowsService("RunBay", *addr, *dataPath, *logPath); err != nil {
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

	if err := runDaemon(*addr, *dataPath, *logPath, stop); err != nil {
		log.Fatal(err)
	}
}

func runDaemon(addr, dataPath, logPath string, stop <-chan struct{}) error {
	logManager := logfiles.New(logPath, logfiles.DefaultRetentionDays)
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
		log.Printf("log data: %s", logPath)
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

	switch runtime.GOOS {
	case "windows":
		if dir := os.Getenv("ProgramData"); dir != "" {
			return dir + string(os.PathSeparator) + "RunBayd" + string(os.PathSeparator) + "tasks.json"
		}
		return `C:\ProgramData\RunBayd\tasks.json`
	case "darwin":
		return "/Library/Application Support/RunBayd/tasks.json"
	default:
		return "/var/lib/runbayd/tasks.json"
	}
}
