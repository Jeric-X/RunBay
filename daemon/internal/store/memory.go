package store

import (
	"crypto/rand"
	"encoding/json"
	"encoding/hex"
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
	logs     map[string][]string
	dataPath string
}

func NewMemoryStore() *MemoryStore {
	return NewMemoryStoreWithPath("")
}

func NewMemoryStoreWithPath(dataPath string) *MemoryStore {
	store := &MemoryStore{
		tasks: make(map[string]*task.Task),
		logs:  make(map[string][]string),
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
	t := &task.Task{
		ID:                  newID(),
		Name:                req.Name,
		Command:             req.Command,
		Cwd:                 req.Cwd,
		Env:                 req.Env,
		StartOnLaunch:       req.StartOnLaunch,
		Status:              task.StatusExited,
		CreatedAt:           now,
		UpdatedAt:           now,
	}

	s.mu.Lock()
	defer s.mu.Unlock()
	s.tasks[t.ID] = cloneTask(t)
	s.logs[t.ID] = []string{now.Format(time.RFC3339) + " task created"}
	if err := s.saveLocked(); err != nil {
		return nil, err
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
	if err := s.saveLocked(); err != nil {
		return err
	}
	return nil
}

func (s *MemoryStore) AppendLog(id, line string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	const maxLines = 5000
	s.logs[id] = append(s.logs[id], line)
	if len(s.logs[id]) > maxLines {
		s.logs[id] = append([]string(nil), s.logs[id][len(s.logs[id])-maxLines:]...)
	}
}

func (s *MemoryStore) TailLogs(id string, tail int) ([]string, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	lines, ok := s.logs[id]
	if !ok {
		return nil, ErrNotFound
	}
	if tail <= 0 || tail > len(lines) {
		tail = len(lines)
	}
	out := append([]string(nil), lines[len(lines)-tail:]...)
	return out, nil
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
			s.logs[t.ID] = []string{time.Now().UTC().Format(time.RFC3339) + " task loaded"}
		}
	}
	return nil
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
