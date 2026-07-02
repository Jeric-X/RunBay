# RunBay

RunBay is a local daemon plus desktop client for managing long-running scripts.

This repository currently contains a first working skeleton:

- `daemon`: Go HTTP daemon for task registry, process supervision, and log tailing.
- `qt-client`: Qt Widgets desktop client that talks to the daemon API.

## First milestone

The daemon listens on `127.0.0.1:8732` by default and exposes:

- `GET /api/health`
- `GET /api/tasks`
- `POST /api/tasks`
- `GET /api/tasks/{id}`
- `DELETE /api/tasks/{id}`
- `POST /api/tasks/{id}/start`
- `POST /api/tasks/{id}/stop`
- `POST /api/tasks/{id}/restart`
- `GET /api/tasks/{id}/logs?tail=500`

The current daemon uses an in-memory task registry so the first loop stays easy
to run and debug, with task definitions persisted to a JSON file. SQLite,
scheduling, service installation, and WebSocket streaming are the next natural
steps.

## Run locally

Daemon:

```powershell
cd daemon
go run ./cmd/runbayd
```

By default, task definitions are saved in a machine-level location suitable for
running the daemon as a system service:

```text
%ProgramData%\RunBay\tasks.json
```

On non-Windows systems the default is `/var/lib/runbay/tasks.json`. You can
override the path when running without service permissions:

```powershell
go run ./cmd/runbayd -data .\runbay-tasks.json
```

Qt client:

```powershell
cd qt-client
cmake -S . -B build\Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build\Debug --config Debug
.\build\Debug\bin\runbay-client.exe
```

If Qt is installed but `cmake` or Qt tools are not in `PATH`, open `qt-client`
with Qt Creator and select your installed Qt 6 kit.

Helper scripts:

```powershell
.\scripts\build-daemon.ps1
.\scripts\build-qt.ps1 -QtPrefix C:\Qt\6.8.0\msvc2022_64
.\scripts\build-all.ps1 -QtPrefix C:\Qt\6.8.0\msvc2022_64
.\scripts\build-all.ps1 -C Release -QtPrefix C:\Qt\6.8.0\msvc2022_64
.\scripts\package-windows.ps1 -C Release -QtPrefix C:\Qt\6.8.0\msvc2022_64
```

Build outputs are separated by configuration:

- daemon: `daemon\bin\Debug\runbayd.exe` or `daemon\bin\Release\runbayd.exe`
- Qt client: `qt-client\build\Debug\bin\runbay-client.exe` or `qt-client\build\Release\bin\runbay-client.exe`

Windows packages are written to `dist\<Configuration>` and include both
`runbayd.exe`, `runbay-client.exe`, and Qt DLL/plugin dependencies copied by
`windeployqt`. The package script skips Qt translations, software OpenGL, and
the system D3D compiler payload to keep the Windows package smaller. It does
not include the MSVC runtime installer by default; pass
`-IncludeCompilerRuntime` if you want `windeployqt --compiler-runtime`.

The Windows package also includes service helper scripts. Run these from an
elevated PowerShell:

```powershell
.\install-service.ps1
.\uninstall-service.ps1
```

When installed as the `RunBay` Windows service, `runbayd.exe` uses the native
Windows Service Control Manager protocol. The Qt client checks `/api/health`
on startup; if the daemon is not reachable, it checks the `RunBay` service and
tries to start it if installed.

VS Code:

- Install the Go extension for `RunBay: Debug daemon (Go)`.
- Install the C/C++ extension for Qt client debugging.
- If Qt is not discoverable automatically, set `runbay.qtPrefix` in
  `.vscode/settings.json` to your Qt kit path, for example
  `C:\Qt\6.8.0\msvc2022_64`.
- Use `RunBay: Debug Qt client (MSVC)` for MSVC kits or
  `RunBay: Debug Qt client (MinGW)` for MinGW kits.
