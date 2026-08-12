package serviceconfig

import (
	"encoding/json"
	"fmt"
	"os"
	"strings"
)

const CurrentVersion = 1

// Config is the platform-neutral definition used to start a RunBay daemon.
// Service managers such as Windows SCM and macOS launchd only point runbayd at
// this file; runtime settings live here rather than in platform registration.
type Config struct {
	Version       int    `json:"version"`
	ServiceID     string `json:"service_id"`
	Name          string `json:"name"`
	ListenAddress string `json:"listen_address"`
	DataFile      string `json:"data_file"`
	LogDirectory  string `json:"log_directory"`
	User          string `json:"user"`
}

func Load(path string) (Config, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return Config{}, fmt.Errorf("read service config: %w", err)
	}

	var config Config
	decoder := json.NewDecoder(strings.NewReader(string(data)))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&config); err != nil {
		return Config{}, fmt.Errorf("decode service config: %w", err)
	}
	if err := config.Validate(); err != nil {
		return Config{}, err
	}
	return config, nil
}

func (c Config) Validate() error {
	if c.Version != CurrentVersion {
		return fmt.Errorf("unsupported service config version %d", c.Version)
	}
	if strings.TrimSpace(c.ServiceID) == "" {
		return fmt.Errorf("service config field service_id is required")
	}
	if strings.TrimSpace(c.ListenAddress) == "" {
		return fmt.Errorf("service config field listen_address is required")
	}
	if strings.TrimSpace(c.DataFile) == "" {
		return fmt.Errorf("service config field data_file is required")
	}
	if strings.TrimSpace(c.LogDirectory) == "" {
		return fmt.Errorf("service config field log_directory is required")
	}
	return nil
}
