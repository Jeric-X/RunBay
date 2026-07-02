package api

import (
	"encoding/json"
	"errors"
	"net/http"
	"strconv"
	"strings"

	"runbay/daemon/internal/process"
	"runbay/daemon/internal/store"
	"runbay/daemon/internal/task"
)

type Server struct {
	store   *store.MemoryStore
	manager *process.Manager
}

func NewServer(store *store.MemoryStore, manager *process.Manager) *Server {
	return &Server{store: store, manager: manager}
}

func (s *Server) Routes() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/api/health", s.health)
	mux.HandleFunc("/api/tasks", s.tasks)
	mux.HandleFunc("/api/tasks/", s.taskByID)
	return withCORS(mux)
}

func (s *Server) health(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
}

func (s *Server) tasks(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		writeJSON(w, http.StatusOK, s.store.List())
	case http.MethodPost:
		var req task.CreateRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			writeError(w, http.StatusBadRequest, "invalid json")
			return
		}
		req.Name = strings.TrimSpace(req.Name)
		req.Command = strings.TrimSpace(req.Command)
		if req.Name == "" || req.Command == "" {
			writeError(w, http.StatusBadRequest, "name and command are required")
			return
		}
		t, err := s.store.Create(req)
		if err != nil {
			writeError(w, http.StatusInternalServerError, err.Error())
			return
		}
		writeJSON(w, http.StatusCreated, t)
	default:
		writeError(w, http.StatusMethodNotAllowed, "method not allowed")
	}
}

func (s *Server) taskByID(w http.ResponseWriter, r *http.Request) {
	parts := strings.Split(strings.TrimPrefix(r.URL.Path, "/api/tasks/"), "/")
	if len(parts) == 0 || parts[0] == "" {
		writeError(w, http.StatusNotFound, "not found")
		return
	}
	id := parts[0]
	action := ""
	if len(parts) > 1 {
		action = parts[1]
	}

	if action == "" {
		s.taskResource(w, r, id)
		return
	}

	switch action {
	case "start":
		s.action(w, r, id, s.manager.Start)
	case "stop":
		s.action(w, r, id, s.manager.Stop)
	case "restart":
		s.action(w, r, id, s.manager.Restart)
	case "logs":
		s.logs(w, r, id)
	default:
		writeError(w, http.StatusNotFound, "not found")
	}
}

func (s *Server) taskResource(w http.ResponseWriter, r *http.Request, id string) {
	switch r.Method {
	case http.MethodGet:
		t, err := s.store.Get(id)
		if err != nil {
			writeStoreError(w, err)
			return
		}
		writeJSON(w, http.StatusOK, t)
	case http.MethodPut:
		var req task.UpdateRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			writeError(w, http.StatusBadRequest, "invalid json")
			return
		}
		req.Name = strings.TrimSpace(req.Name)
		req.Command = strings.TrimSpace(req.Command)
		if req.Name == "" || req.Command == "" {
			writeError(w, http.StatusBadRequest, "name and command are required")
			return
		}
		t, err := s.store.Get(id)
		if err != nil {
			writeStoreError(w, err)
			return
		}
		t.Name = req.Name
		t.Command = req.Command
		t.Cwd = req.Cwd
		t.Env = req.Env
		t.StartOnLaunch = req.StartOnLaunch
		if err := s.store.Update(t); err != nil {
			writeStoreError(w, err)
			return
		}
		writeJSON(w, http.StatusOK, t)
	case http.MethodDelete:
		if _, err := s.manager.Stop(id); err != nil && !errors.Is(err, store.ErrNotFound) {
			writeError(w, http.StatusInternalServerError, err.Error())
			return
		}
		if err := s.store.Delete(id); err != nil {
			writeStoreError(w, err)
			return
		}
		w.WriteHeader(http.StatusNoContent)
	default:
		writeError(w, http.StatusMethodNotAllowed, "method not allowed")
	}
}

func (s *Server) action(w http.ResponseWriter, r *http.Request, id string, fn func(string) (*task.Task, error)) {
	if r.Method != http.MethodPost {
		writeError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}
	t, err := fn(id)
	if err != nil {
		writeStoreError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, t)
}

func (s *Server) logs(w http.ResponseWriter, r *http.Request, id string) {
	if r.Method != http.MethodGet {
		writeError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}
	tail, _ := strconv.Atoi(r.URL.Query().Get("tail"))
	after, _ := strconv.ParseUint(r.URL.Query().Get("after"), 10, 64)
	logs, err := s.store.TailLogs(id, tail, after)
	if err != nil {
		writeStoreError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, logs)
}

func writeStoreError(w http.ResponseWriter, err error) {
	if errors.Is(err, store.ErrNotFound) {
		writeError(w, http.StatusNotFound, err.Error())
		return
	}
	if errors.Is(err, process.ErrAlreadyRunning) {
		writeError(w, http.StatusConflict, err.Error())
		return
	}
	writeError(w, http.StatusInternalServerError, err.Error())
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}

func writeError(w http.ResponseWriter, status int, message string) {
	writeJSON(w, status, map[string]string{"error": message})
}

func withCORS(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Access-Control-Allow-Origin", "http://127.0.0.1")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type, Authorization")
		w.Header().Set("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
		if r.Method == http.MethodOptions {
			w.WriteHeader(http.StatusNoContent)
			return
		}
		next.ServeHTTP(w, r)
	})
}
