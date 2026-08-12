package serviceconfig

import (
	"os"
	"path/filepath"
	"testing"
)

func TestLoad(t *testing.T) {
	path := filepath.Join(t.TempDir(), "service.json")
	content := `{
  "version": 1,
  "service_id": "runbay-1234",
  "name": "RunBay",
  "listen_address": "127.0.0.1:8732",
  "data_file": "/tmp/runbay/tasks.json",
  "log_directory": "/tmp/runbay/logs",
  "user": "runner"
}`
	if err := os.WriteFile(path, []byte(content), 0600); err != nil {
		t.Fatal(err)
	}

	config, err := Load(path)
	if err != nil {
		t.Fatal(err)
	}
	if config.ListenAddress != "127.0.0.1:8732" || config.User != "runner" {
		t.Fatalf("unexpected config: %#v", config)
	}
}

func TestLoadRejectsUnknownFields(t *testing.T) {
	path := filepath.Join(t.TempDir(), "service.json")
	if err := os.WriteFile(path, []byte(`{
  "version": 1,
  "service_id": "runbay-1234",
  "listen_address": "127.0.0.1:8732",
  "data_file": "/tmp/tasks.json",
  "log_directory": "/tmp/logs",
  "legacy": true
}`), 0600); err != nil {
		t.Fatal(err)
	}

	if _, err := Load(path); err == nil {
		t.Fatal("expected unknown field to be rejected")
	}
}
