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

On Linux the default is `/var/lib/runbayd/tasks.json`; on macOS it is
`/Library/Application Support/RunBayd/tasks.json`. You can override the path
when running without service permissions:

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

## macOS app and boot service

On macOS, package the Qt client as a native app bundle with the daemon embedded
in `Contents/MacOS`:

```bash
brew install go cmake qtbase yaml-cpp
./scripts/package-macos.sh
open ./dist/Release/RunBay.app
```

The packaging script runs `macdeployqt`, performs an ad-hoc signature, and
writes the self-contained app to `dist/Release/RunBay.app`. It packages the
native architecture by default. A universal package can be requested when the
Qt installation contains both slices:

```bash
./scripts/package-macos.sh --architectures 'arm64;x86_64' --qt-prefix /path/to/universal/Qt
```

Install the third-party `create-dmg` tool and create a styled DMG containing
the app and an Applications shortcut with:

```bash
brew install create-dmg
./scripts/create-macos-dmg.sh \
  --app ./dist/Release/RunBay.app \
  --output ./dist/RunBay-macos-Release.dmg
```

The DMG uses the 660×400 background and icon layout under `packaging/macos`.
The GitHub Actions workflow installs `create-dmg` with Homebrew and uses the
same script and assets as local packaging.

macOS uses `launchd` as the equivalent of the Windows Service Control Manager.
RunBay uses a system `LaunchDaemon`, rather than a per-user `LaunchAgent`, so
the daemon starts during boot and does not require a logged-in user. Open
`Service` -> `Manage Services...` in the desktop app to:

- add a service and select a local macOS user;
- register or unregister it through the standard administrator authorization prompt;
- start and stop it from the UI;
- inspect its current launchd state.

Tasks spawned by the daemon run as the selected service user. The app creates
that user's data and log directories during registration. The command-line
installer remains available for a default root-owned service:

```bash
sudo ./dist/Release/install-launchdaemon.sh
```

The generated LaunchDaemon directly runs
`RunBay.app/Contents/MacOS/runbayd`; it does not copy the daemon to a separate
system directory. UI-created launchd labels use
`com.runbay.daemon.<service-id>`. The minimal generated plist contains the app's
daemon path, selected user, boot policy, and a pointer to `service.json`;
listen address, data file, and log directory are stored in that
platform-neutral JSON configuration instead of the plist.

To uninstall the boot service while retaining task data and logs:

```bash
sudo ./dist/Release/uninstall-launchdaemon.sh
```

The default root service stores task data under
`/Library/Application Support/RunBayd` and logs under `/Library/Logs/RunBayd`.
Services running as normal users default to that user's
`~/Library/Application Support/RunBayd` directory. Because launchd directly
references the registered app bundle, moving or deleting `RunBay.app` requires
unregistering and registering the service again from the app's new location.

## Platform-neutral service configuration

Both Windows SCM registration and macOS launchd registration start the daemon
with `runbayd -service-config <path>`. The referenced JSON is the authoritative
runtime configuration:

```json
{
  "version": 1,
  "service_id": "runbay-0123456789ab",
  "name": "RunBay",
  "listen_address": "127.0.0.1:8732",
  "data_file": "/path/to/RunBayd/tasks.json",
  "log_directory": "/path/to/RunBayd/logs",
  "user": "service-user"
}
```

Platform service definitions retain only values required by the operating
system, such as the Windows service account or launchd's `UserName` and label.
The configuration schema is strict and versioned; unknown fields and unsupported
versions are rejected.

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

Register, start, stop, and remove Windows services from the Qt client's
Service menu. The Windows package does not include separate service management
scripts.

When installed as the `RunBay` Windows service, `runbayd.exe` uses the native
Windows Service Control Manager protocol and reads the same `service.json`
format used on macOS. The Qt client checks `/api/health`
and shows service state in the status bar, including whether the service is
installed, running, and configured for automatic startup. It does not install
or start the service automatically; use the Service menu to install, start,
stop, or delete it.

### Windows service account setup

When registering a service to run as a normal Windows user account, configure
the account before starting the service:

1. Open `secpol.msc` from the Run dialog or Start menu.
2. Go to `Local Policies` -> `User Rights Assignment`.
3. Open `Log on as a service`.
4. Add the service account, for example `.\username` or
   `COMPUTERNAME\username`.
5. Make sure the account can read and write the selected RunBay data directory.
6. Register and start the service from the Qt client's Service menu.

Windows Home may not include `secpol.msc`. In domain-managed environments, Group
Policy can overwrite this local setting; ask the domain administrator to grant
`Log on as a service` to the account if the setting does not persist.

Daemon and task log files are written under `%ProgramData%\RunBay\logs` on
Windows. Logs are grouped by date folder and old folders are pruned daily, with
the latest seven days retained.

## GitHub Actions

The `Package Windows` and `Package macOS` workflows run on every push and can
also be started manually from the Actions tab. Windows builds the Release
daemon and Qt client on `windows-latest`, then uploads
`RunBay-windows-Release.zip`. macOS builds both binaries on `macos-14`, embeds
the daemon and Qt frameworks in `RunBay.app`, creates
`RunBay-macos-Release.dmg`, verifies it, and uploads the DMG as the
`RunBay-macos-Release` artifact.

VS Code:

- Install the Go extension for `RunBay: Debug daemon (Go)`.
- Install the C/C++ extension for Qt client debugging.
- If Qt is not discoverable automatically, set `runbay.qtPrefix` in
  `.vscode/settings.json` to your Qt kit path, for example
  `C:\Qt\6.8.3\msvc2022_64`.
- Use `RunBay: Debug Qt client (MSVC)` for MSVC kits or
  `RunBay: Debug Qt client (MinGW)` for MinGW kits.
