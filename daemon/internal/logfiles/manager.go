package logfiles

import (
	"fmt"
	"io"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"time"
)

const DefaultRetentionDays = 7

type Manager struct {
	mu            sync.Mutex
	root          string
	retentionDays int
	files         map[string]*os.File
}

func New(root string, retentionDays int) *Manager {
	if retentionDays <= 0 {
		retentionDays = DefaultRetentionDays
	}
	return &Manager{
		root:          root,
		retentionDays: retentionDays,
		files:         make(map[string]*os.File),
	}
}

func DefaultRoot() string {
	if value := os.Getenv("RUNBAY_LOG_DIR"); value != "" {
		return value
	}

	switch runtime.GOOS {
	case "windows":
		if dir := os.Getenv("ProgramData"); dir != "" {
			return filepath.Join(dir, "RunBayd", "logs")
		}
		return `C:\ProgramData\RunBayd\logs`
	default:
		return filepath.Join(os.TempDir(), "runbayd", "logs")
	}
}

func (m *Manager) AppendTaskLine(taskID, line string) {
	m.appendLine(filepath.Join("tasks", safeName(taskID)+".log"), line)
}

func (m *Manager) DaemonWriter() io.Writer {
	return daemonWriter{manager: m}
}

func (m *Manager) CleanupExpired() error {
	if m.root == "" {
		return nil
	}

	entries, err := os.ReadDir(m.root)
	if err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return err
	}

	cutoff := time.Now().AddDate(0, 0, -m.retentionDays+1)
	cutoffDate := dateString(cutoff)
	for _, entry := range entries {
		if !entry.IsDir() {
			continue
		}
		name := entry.Name()
		if _, err := time.Parse("2006-01-02", name); err != nil {
			continue
		}
		if name < cutoffDate {
			expiredPath := filepath.Join(m.root, name)
			if err := m.closeFilesUnder(expiredPath); err != nil {
				return err
			}
			if err := os.RemoveAll(expiredPath); err != nil {
				return err
			}
		}
	}
	return nil
}

func (m *Manager) StartCleanupLoop(stop <-chan struct{}) {
	go func() {
		_ = m.CleanupExpired()
		ticker := time.NewTicker(24 * time.Hour)
		defer ticker.Stop()
		for {
			select {
			case <-stop:
				return
			case <-ticker.C:
				_ = m.CleanupExpired()
			}
		}
	}()
}

func (m *Manager) Close() error {
	m.mu.Lock()
	defer m.mu.Unlock()

	var closeErr error
	for key, file := range m.files {
		if err := file.Close(); err != nil && closeErr == nil {
			closeErr = err
		}
		delete(m.files, key)
	}
	return closeErr
}

func (m *Manager) closeFilesUnder(root string) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	var closeErr error
	root = filepath.Clean(root)
	for key, file := range m.files {
		if key != root && !strings.HasPrefix(key, root+string(os.PathSeparator)) {
			continue
		}
		if err := file.Close(); err != nil && closeErr == nil {
			closeErr = err
		}
		delete(m.files, key)
	}
	return closeErr
}

func (m *Manager) appendLine(relativePath, line string) {
	if line == "" {
		line = " "
	}
	if !strings.ContainsAny(line, "\r\n") {
		line += "\n"
	}
	_, _ = m.write(relativePath, []byte(line))
}

func (m *Manager) write(relativePath string, data []byte) (int, error) {
	if m.root == "" {
		return len(data), nil
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	logPath := filepath.Join(m.root, dateString(time.Now()), relativePath)
	file, err := m.fileLocked(logPath)
	if err != nil {
		return 0, err
	}
	return file.Write(data)
}

func (m *Manager) fileLocked(path string) (*os.File, error) {
	if file, ok := m.files[path]; ok {
		return file, nil
	}
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		return nil, err
	}
	file, err := os.OpenFile(path, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
	if err != nil {
		return nil, err
	}
	m.files[path] = file
	return file, nil
}

func dateString(t time.Time) string {
	return t.Local().Format("2006-01-02")
}

func safeName(value string) string {
	value = strings.TrimSpace(value)
	if value == "" {
		return "unknown"
	}

	var builder strings.Builder
	for _, r := range value {
		if (r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z') || (r >= '0' && r <= '9') || r == '-' || r == '_' || r == '.' {
			builder.WriteRune(r)
		} else {
			builder.WriteByte('_')
		}
	}
	if builder.Len() == 0 {
		return fmt.Sprintf("log-%d", time.Now().UnixNano())
	}
	return builder.String()
}

type daemonWriter struct {
	manager *Manager
}

func (w daemonWriter) Write(data []byte) (int, error) {
	if w.manager == nil {
		return len(data), nil
	}
	return w.manager.write("daemon.log", data)
}
