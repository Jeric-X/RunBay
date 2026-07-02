# RunBay

RunBay is a local daemon plus desktop client for managing long-running scripts.

The project is split into two components:

- `daemon`: Go HTTP daemon for task registry, process supervision, and log tailing.
- `qt-client`: Qt Widgets desktop client that talks to the daemon API.

## API

The daemon listens on `127.0.0.1:8732` by default and exposes:

- `GET /api/health`
- `GET /api/tasks`
- `POST /api/tasks`
- `GET /api/tasks/{id}`
- `DELETE /api/tasks/{id}`
- `POST /api/tasks/{id}/start`
- `POST /api/tasks/{id}/stop`
- `POST /api/tasks/{id}/restart`
- `GET /api/tasks/{id}/logs?tail=500&after=123`

Task definitions are persisted to a JSON file. Task logs are returned as
ordered entries with monotonically increasing ids, so clients can request only
entries after the last id they have rendered.

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
.\scripts\build-qt.ps1 -QtPrefix C:\Qt\6.8.3\msvc2022_64
.\scripts\build-all.ps1 -QtPrefix C:\Qt\6.8.3\msvc2022_64
.\scripts\build-all.ps1 -C Release -QtPrefix C:\Qt\6.8.3\msvc2022_64
.\scripts\package-windows.ps1 -C Release -QtPrefix C:\Qt\6.8.3\msvc2022_64
.\scripts\package-windows.ps1 -C Release -T Client -QtPrefix C:\Qt\6.8.3\msvc2022_64
.\scripts\package-windows.ps1 -C Release -T Daemon
```

Build outputs are separated by configuration:

- daemon: `daemon\bin\Debug\runbayd.exe` or `daemon\bin\Release\runbayd.exe`
- Qt client: `qt-client\build\Debug\bin\runbay-client.exe` or `qt-client\build\Release\bin\runbay-client.exe`

Windows packages are written to `dist\<Configuration>`. By default,
`package-windows.ps1` packages both `runbayd.exe` and `runbay-client.exe`; pass
`-T Client` or `-T Daemon` to package only one side. The shorthand `-T` is an
alias for `-Target`. Partial packaging updates the existing output directory in
place, while the default `-T All` cleans the output directory unless `-NoClean`
is passed.

The client package includes Qt DLL/plugin dependencies copied by `windeployqt`.
The package script skips Qt translations, software OpenGL, the system D3D
compiler payload, and the MSVC runtime installer to keep the Windows package
smaller. Pass `-IncludeCompilerRuntime` if you want
`windeployqt --compiler-runtime`.

The Windows package also includes service helper scripts. Run these from an
elevated PowerShell:

```powershell
.\install-service.ps1
.\uninstall-service.ps1
```

When installed as the `RunBay` Windows service, `runbayd.exe` uses the native
Windows Service Control Manager protocol. The Qt client checks `/api/health`
and shows service state in the status bar, including whether the service is
installed, running, and configured for automatic startup. It does not install
or start the service automatically; use the Service menu to install, start,
stop, or delete it.

Daemon and task log files are written under `%ProgramData%\RunBay\logs` on
Windows. Logs are grouped by date folder and old folders are pruned daily, with
the latest seven days retained.

## GitHub Actions

The `Package Windows` workflow runs on every push and can also be started
manually from the Actions tab. It builds the Release daemon and Qt client on
`windows-latest` with Go 1.22, MSVC x64, and Qt 6.8.3
`win64_msvc2022_64`, then packages `dist\Release` into
`dist\RunBay-windows-Release.zip` and uploads it as the
`RunBay-windows-Release` artifact.

VS Code:

- Install the Go extension for `RunBay: Debug daemon (Go)`.
- Install the C/C++ extension for Qt client debugging.
- If Qt is not discoverable automatically, set `runbay.qtPrefix` in
  `.vscode/settings.json` to your Qt kit path, for example
  `C:\Qt\6.8.3\msvc2022_64`.
- Use `RunBay: Debug Qt client (MSVC)` for MSVC kits or
  `RunBay: Debug Qt client (MinGW)` for MinGW kits.
