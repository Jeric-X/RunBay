# RunBay Local API

Base URL:

```text
http://127.0.0.1:8732
```

## Health

```http
GET /api/health
```

Response:

```json
{
  "status": "ok"
}
```

## Tasks

```http
GET /api/tasks
```

```http
POST /api/tasks
Content-Type: application/json
```

Request:

```json
{
  "name": "backup-db",
  "command": "python backup.py",
  "cwd": "C:/jobs/backup",
  "start_on_launch": true,
  "env": {
    "NODE_ENV": "production"
  }
}
```

`start_on_launch` controls whether `runbayd` starts the task automatically when the daemon launches. If it is `false` or omitted, the task stays stopped until started manually.

```http
PUT /api/tasks/{id}
Content-Type: application/json
```

Uses the same request body as task creation to update name, command, working directory, environment, and startup behavior.

## Task Actions

```http
POST /api/tasks/{id}/start
POST /api/tasks/{id}/stop
POST /api/tasks/{id}/restart
```

## Logs

```http
GET /api/tasks/{id}/logs?tail=500
```

Response:

```json
{
  "task_id": "task-id",
  "lines": [
    "2026-07-01T10:00:00Z task created"
  ]
}
```
