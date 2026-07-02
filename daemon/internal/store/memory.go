package store

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"sort"
	"sync"
	"time"

	"runbay/daemon/internal/task"
)

var ErrNotFound = errors.New("task not found")

type MemoryStore struct {
	mu       sync.RWMutex
	tasks    map[string]*task.Task
	logs     map[string][]task.LogEntry
	nextLog  map[string]uint64
	dataPath string
	logSink  LogSink
}

type LogSink interface {
	AppendTaskLine(taskID, line string)
}

func NewMemoryStore() *MemoryStore {
	return NewMemoryStoreWithPath("")
}

func NewMemoryStoreWithPath(dataPath string) *MemoryStore {
	return NewMemoryStoreWithPathAndLogSink(dataPath, nil)
}

func NewMemoryStoreWithPathAndLogSink(dataPath string, logSink LogSink) *MemoryStore {
	store := &MemoryStore{
		tasks:   make(map[string]*task.Task),
		logs:    make(map[string][]task.LogEntry),
		nextLog: make(map[string]uint64),
		logSink: logSink,
	}
	if dataPath != "" {
		store.dataPath = dataPath
		if err := store.load(); err != nil {
			store.AppendLog("system", "failed to load task store: "+err.Error())
		}
	}
	return store
}

func (s *MemoryStore) Create(req task.CreateRequest) (*task.Task, error) {
	now := time.Now().UTC()
	createdLine := now.Format(time.RFC3339) + " task created"
	t := &task.Task{
		ID:            newID(),
		Name:          req.Name,
		Command:       req.Command,
		Cwd:           req.Cwd,
		Env:           req.Env,
		StartOnLaunch: req.StartOnLaunch,
		Status:        task.StatusExited,
		CreatedAt:     now,
		UpdatedAt:     now,
	}

	s.mu.Lock()
	s.tasks[t.ID] = cloneTask(t)
	s.logs[t.ID] = []task.LogEntry{s.newLogEntryLocked(t.ID, createdLine)}
	if err := s.saveLocked(); err != nil {
		s.mu.Unlock()
		return nil, err
	}
	logSink := s.logSink
	s.mu.Unlock()

	if logSink != nil {
		logSink.AppendTaskLine(t.ID, createdLine)
	}
	return cloneTask(t), nil
}

func (s *MemoryStore) List() []*task.Task {
	s.mu.RLock()
	defer s.mu.RUnlock()

	items := make([]*task.Task, 0, len(s.tasks))
	for _, t := range s.tasks {
		items = append(items, cloneTask(t))
	}
	sort.Slice(items, func(i, j int) bool {
		return items[i].CreatedAt.Before(items[j].CreatedAt)
	})
	return items
}

func (s *MemoryStore) Get(id string) (*task.Task, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	t, ok := s.tasks[id]
	if !ok {
		return nil, ErrNotFound
	}
	return cloneTask(t), nil
}

func (s *MemoryStore) Update(t *task.Task) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if _, ok := s.tasks[t.ID]; !ok {
		return ErrNotFound
	}
	t.UpdatedAt = time.Now().UTC()
	s.tasks[t.ID] = cloneTask(t)
	if err := s.saveLocked(); err != nil {
		return err
	}
	return nil
}

func (s *MemoryStore) Delete(id string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if _, ok := s.tasks[id]; !ok {
		return ErrNotFound
	}
	delete(s.tasks, id)
	delete(s.logs, id)
	delete(s.nextLog, id)
	if err := s.saveLocked(); err != nil {
		return err
	}
	return nil
}

func (s *MemoryStore) AppendLog(id, line string) {
	s.mu.Lock()
	const maxLines = 5000
	s.logs[id] = append(s.logs[id], s.newLogEntryLocked(id, line))
	if len(s.logs[id]) > maxLines {
		s.logs[id] = append([]task.LogEntry(nil), s.logs[id][len(s.logs[id])-maxLines:]...)
	}
	logSink := s.logSink
	s.mu.Unlock()

	if logSink != nil {
		logSink.AppendTaskLine(id, line)
	}
}

func (s *MemoryStore) TailLogs(id string, tail int, after uint64) (*task.LogResponse, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	entries, ok := s.logs[id]
	if !ok {
		return nil, ErrNotFound
	}

	start := 0
	truncated := false
	if after > 0 {
		for start < len(entries) && entries[start].ID <= after {
			start++
		}
		if len(entries) > 0 && after < entries[0].ID-1 {
			truncated = true
			start = 0
		}
	} else {
		if tail <= 0 || tail > len(entries) {
			tail = len(entries)
		}
		start = len(entries) - tail
	}

	out := append([]task.LogEntry(nil), entries[start:]...)
	response := &task.LogResponse{
		TaskID:    id,
		Truncated: truncated,
		Entries:   out,
	}
	if len(out) > 0 {
		response.StartID = out[0].ID
		response.EndID = out[len(out)-1].ID
	} else if len(entries) > 0 {
		response.StartID = entries[0].ID
		response.EndID = entries[len(entries)-1].ID
	}
	return response, nil
}

func (s *MemoryStore) load() error {
	s.mu.Lock()
	defer s.mu.Unlock()

	data, err := os.ReadFile(s.dataPath)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return nil
		}
		return err
	}

	var snapshot storeSnapshot
	if err := json.Unmarshal(data, &snapshot); err != nil {
		return err
	}

	for i := range snapshot.Tasks {
		t := snapshot.Tasks[i]
		if t == nil {
			continue
		}
		t.Status = task.StatusExited
		t.PID = 0
		t.StartedAt = nil
		t.ExitedAt = nil
		s.tasks[t.ID] = cloneTask(t)
		if _, ok := s.logs[t.ID]; !ok {
			s.logs[t.ID] = []task.LogEntry{s.newLogEntryLocked(t.ID, time.Now().UTC().Format(time.RFC3339)+" task loaded")}
		}
	}
	return nil
}

func (s *MemoryStore) newLogEntryLocked(id, text string) task.LogEntry {
	next := s.nextLog[id] + 1
	s.nextLog[id] = next
	return task.LogEntry{
		ID:   next,
		Text: text,
	}
}

func (s *MemoryStore) saveLocked() error {
	if s.dataPath == "" {
		return nil
	}

	items := make([]*task.Task, 0, len(s.tasks))
	for _, t := range s.tasks {
		cp := cloneTask(t)
		cp.Status = task.StatusExited
		cp.PID = 0
		cp.StartedAt = nil
		cp.ExitedAt = nil
		items = append(items, cp)
	}
	sort.Slice(items, func(i, j int) bool {
		return items[i].CreatedAt.Before(items[j].CreatedAt)
	})

	snapshot := storeSnapshot{
		Version: 1,
		Tasks:   items,
	}
	data, err := json.MarshalIndent(snapshot, "", "  ")
	if err != nil {
		return err
	}

	if err := os.MkdirAll(filepath.Dir(s.dataPath), 0755); err != nil {
		return err
	}
	tmpPath := s.dataPath + ".tmp"
	if err := os.WriteFile(tmpPath, data, 0644); err != nil {
		return err
	}
	return os.Rename(tmpPath, s.dataPath)
}

type storeSnapshot struct {
	Version int          `json:"version"`
	Tasks   []*task.Task `json:"tasks"`
}

func cloneTask(t *task.Task) *task.Task {
	cp := *t
	if t.Env != nil {
		cp.Env = make(map[string]string, len(t.Env))
		for k, v := range t.Env {
			cp.Env[k] = v
		}
	}
	return &cp
}

func newID() string {
	var b [8]byte
	if _, err := rand.Read(b[:]); err != nil {
		return hex.EncodeToString([]byte(time.Now().UTC().Format("20060102150405.000000000")))
	}
	return hex.EncodeToString(b[:])
}
