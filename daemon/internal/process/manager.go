package process

import (
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"runtime"
	"sync"
	"time"

	"runbay/daemon/internal/store"
	"runbay/daemon/internal/task"
	"runbay/daemon/internal/textdecode"
)

var ErrAlreadyRunning = errors.New("task is already running")

type Manager struct {
	mu      sync.Mutex
	store   *store.MemoryStore
	running map[string]*managedProcess
}

func NewManager(store *store.MemoryStore) *Manager {
	return &Manager{
		store:   store,
		running: make(map[string]*managedProcess),
	}
}

type managedProcess struct {
	cmd           *exec.Cmd
	done          chan struct{}
	requestedStop bool
}

func (m *Manager) Start(id string) (*task.Task, error) {
	m.mu.Lock()
	if _, ok := m.running[id]; ok {
		m.mu.Unlock()
		return nil, ErrAlreadyRunning
	}
	t, err := m.store.Get(id)
	if err != nil {
		m.mu.Unlock()
		return nil, err
	}
	t.Status = task.StatusStarting
	t.ExitCode = nil
	t.ExitedAt = nil
	if err := m.store.Update(t); err != nil {
		m.mu.Unlock()
		return nil, err
	}

	cmd := shellCommand(t.Command)
	cmd.Dir = t.Cwd
	cmd.Env = processEnv(t.Env)

	stdout, err := cmd.StdoutPipe()
	if err != nil {
		m.mu.Unlock()
		return nil, err
	}
	stderr, err := cmd.StderrPipe()
	if err != nil {
		m.mu.Unlock()
		return nil, err
	}
	if err := cmd.Start(); err != nil {
		t.Status = task.StatusFailed
		_ = m.store.Update(t)
		m.mu.Unlock()
		return nil, err
	}

	now := time.Now().UTC()
	t.Status = task.StatusRunning
	t.PID = cmd.Process.Pid
	t.StartedAt = &now
	_ = m.store.Update(t)
	proc := &managedProcess{cmd: cmd, done: make(chan struct{})}
	m.running[id] = proc
	m.mu.Unlock()

	m.store.AppendLog(id, fmt.Sprintf("%s started pid=%d", now.Format(time.RFC3339), cmd.Process.Pid))
	go m.scanOutput(id, stdout)
	go m.scanOutput(id, stderr)
	go m.wait(id, proc)

	return m.store.Get(id)
}

func (m *Manager) Stop(id string) (*task.Task, error) {
	m.mu.Lock()
	proc, ok := m.running[id]
	t, err := m.store.Get(id)
	if err != nil {
		m.mu.Unlock()
		return nil, err
	}
	if !ok {
		m.mu.Unlock()
		return t, nil
	}
	t.Status = task.StatusStopping
	proc.requestedStop = true
	_ = m.store.Update(t)
	m.mu.Unlock()

	m.store.AppendLog(id, time.Now().UTC().Format(time.RFC3339)+" stopping")
	if proc.cmd.Process != nil {
		if err := killProcessTree(proc.cmd); err != nil {
			return nil, err
		}
	}

	select {
	case <-proc.done:
	case <-time.After(5 * time.Second):
		m.store.AppendLog(id, time.Now().UTC().Format(time.RFC3339)+" stop timed out waiting for process exit")
	}
	return m.store.Get(id)
}

func (m *Manager) Restart(id string) (*task.Task, error) {
	if _, err := m.Stop(id); err != nil {
		return nil, err
	}
	time.Sleep(200 * time.Millisecond)
	t, _ := m.store.Get(id)
	if t != nil && t.Status == task.StatusStopping {
		time.Sleep(300 * time.Millisecond)
	}
	return m.Start(id)
}

func (m *Manager) StopAll() {
	m.mu.Lock()
	procs := make([]*managedProcess, 0, len(m.running))
	for _, proc := range m.running {
		procs = append(procs, proc)
	}
	m.mu.Unlock()

	for _, proc := range procs {
		if proc.cmd.Process != nil {
			_ = killProcessTree(proc.cmd)
		}
	}
}

func (m *Manager) scanOutput(id string, r io.Reader) {
	var chunk []byte
	buffer := make([]byte, 4096)

	emit := func() {
		if len(chunk) == 0 {
			return
		}
		text := textdecode.BytesToString(append([]byte(nil), chunk...))
		m.store.AppendLog(id, text)
		chunk = chunk[:0]
	}

	for {
		n, err := r.Read(buffer)
		for _, b := range buffer[:n] {
			switch b {
			case '\r':
				emit()
				chunk = append(chunk, b)
			case '\n':
				chunk = append(chunk, b)
				emit()
			default:
				chunk = append(chunk, b)
			}
		}

		if err != nil {
			emit()
			return
		}
	}
}

func (m *Manager) wait(id string, proc *managedProcess) {
	defer close(proc.done)

	err := proc.cmd.Wait()

	m.mu.Lock()
	requestedStop := proc.requestedStop
	delete(m.running, id)
	m.mu.Unlock()

	t, getErr := m.store.Get(id)
	if getErr != nil {
		return
	}

	now := time.Now().UTC()
	t.PID = 0
	t.ExitedAt = &now
	exitCode := 0
	if err != nil {
		exitCode = 1
		if exitErr, ok := err.(*exec.ExitError); ok {
			exitCode = exitErr.ExitCode()
		}
	}
	t.ExitCode = &exitCode
	if requestedStop || exitCode == 0 {
		t.Status = task.StatusExited
	} else {
		t.Status = task.StatusFailed
	}
	_ = m.store.Update(t)
	m.store.AppendLog(id, fmt.Sprintf("%s exited code=%d", now.Format(time.RFC3339), exitCode))
}

func shellCommand(command string) *exec.Cmd {
	if runtime.GOOS == "windows" {
		return exec.Command("cmd", "/C", command)
	}
	return exec.Command("sh", "-c", command)
}

func envPairs(env map[string]string) []string {
	pairs := make([]string, 0, len(env))
	for k, v := range env {
		pairs = append(pairs, k+"="+v)
	}
	return pairs
}

func processEnv(taskEnv map[string]string) []string {
	env := os.Environ()
	if runtime.GOOS == "windows" {
		env = append(env,
			"PYTHONUTF8=1",
			"PYTHONIOENCODING=utf-8",
		)
	}
	return append(env, envPairs(taskEnv)...)
}
