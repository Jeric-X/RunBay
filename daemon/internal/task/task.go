package task

import "time"

type Status string

const (
	StatusDisabled Status = "disabled"
	StatusExited   Status = "exited"
	StatusFailed   Status = "failed"
	StatusRunning  Status = "running"
	StatusStarting Status = "starting"
	StatusStopping Status = "stopping"
)

type Task struct {
	ID            string            `json:"id"`
	Name          string            `json:"name"`
	Command       string            `json:"command"`
	Cwd           string            `json:"cwd,omitempty"`
	Env           map[string]string `json:"env,omitempty"`
	StartOnLaunch bool              `json:"start_on_launch,omitempty"`
	Status        Status            `json:"status"`
	PID           int               `json:"pid,omitempty"`
	ExitCode      *int              `json:"exit_code,omitempty"`
	StartedAt     *time.Time        `json:"started_at,omitempty"`
	ExitedAt      *time.Time        `json:"exited_at,omitempty"`
	Restart       string            `json:"restart,omitempty"`
	Schedule      string            `json:"schedule,omitempty"`
	CreatedAt     time.Time         `json:"created_at"`
	UpdatedAt     time.Time         `json:"updated_at"`
	RestartCount  int               `json:"restart_count"`
}

type CreateRequest struct {
	Name          string            `json:"name"`
	Command       string            `json:"command"`
	Cwd           string            `json:"cwd"`
	Env           map[string]string `json:"env"`
	StartOnLaunch bool              `json:"start_on_launch"`
}

type UpdateRequest struct {
	Name          string            `json:"name"`
	Command       string            `json:"command"`
	Cwd           string            `json:"cwd"`
	Env           map[string]string `json:"env"`
	StartOnLaunch bool              `json:"start_on_launch"`
}

type LogEntry struct {
	ID   uint64 `json:"id"`
	Text string `json:"text"`
}

type LogResponse struct {
	TaskID    string     `json:"task_id"`
	StartID   uint64     `json:"start_id"`
	EndID     uint64     `json:"end_id"`
	Truncated bool       `json:"truncated"`
	Entries   []LogEntry `json:"entries"`
}
