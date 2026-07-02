# RunBay 项目上下文

我要做一个长期运行脚本的管理工具，类似 Docker/qBittorrent 的控制台。

## 产品目标

- 管理长期运行脚本
- 查看实时输出日志
- 控制任务启动、停止、重启
- 支持计划任务
- 支持特定任务的快速执行脚本
- 跨平台
- 支持用户未登录时自启
- 支持无 UI 环境运行
- 可以有 Qt 管理 UI，但核心不能依赖 Qt

## 项目名

项目名：`RunBay`

含义倾向：运行舱、任务停靠区。适合表达长期脚本、计划任务、快速执行任务都在一个地方停靠和管理。

## 推荐架构

核心采用 daemon/service 架构：

- `runbayd`：后台守护进程，负责任务管理、进程管理、日志、计划任务、自启
- `runbayctl`：CLI 客户端
- Web UI：第一版推荐做内置 Web UI
- Qt UI：可选桌面客户端，未来可以做

Qt/Web/CLI 都通过同一套本地 API 与 daemon 通信。

```text
runbayd
  - task registry
  - process supervisor
  - scheduler
  - log streamer
  - auth / local token
  - platform service installer

runbayctl
  - start/stop/restart task
  - tail logs
  - run quick task
  - install-service / uninstall-service

webui
  - dashboard
  - task detail
  - live log viewer
  - schedule editor
  - quick scripts
  - settings
```

## 推荐技术栈

首选：

- Go 后端 daemon
- SQLite 存储任务、状态、历史记录
- HTTP REST API 管理任务
- WebSocket 推送实时日志和状态
- Svelte 或 React 做 Web UI
- 单二进制发布，前端资源内嵌
- Windows 使用 Windows Service 自启
- Linux 使用 systemd 自启
- macOS 使用 launchd 自启

备选：

- Rust daemon + Web UI
- .NET Worker Service + ASP.NET Core + SignalR

## UI 策略

第一版推荐优先做内置 Web UI，而不是先做 Qt。

原因：

- 无 UI 环境也能用
- 服务器上可以通过浏览器远程访问
- 跨平台成本低
- 实时日志适合用 WebSocket / SSE
- UI 迭代快
- Qt 客户端未来可以调用同一套 API

Qt 不应该负责长期进程生命周期。长期运行、用户未登录时自启、无 UI 环境运行，这些都应该属于 daemon/service。

## 通信设计

第一版推荐：

- daemon 监听 `127.0.0.1:8732`
- REST API 用于任务管理
- WebSocket 用于日志流和状态推送
- 本地 token 鉴权

示例 API：

- `GET /api/tasks`
- `POST /api/tasks/{id}/start`
- `POST /api/tasks/{id}/stop`
- `POST /api/tasks/{id}/restart`
- `GET /api/tasks/{id}/logs?tail=500`
- `WS /api/tasks/{id}/logs/stream`
- `WS /api/events`

Qt 侧可以使用：

- `QNetworkAccessManager` 调 REST API
- `QWebSocket` 接实时日志
- `QJsonDocument` 解析 JSON

后续如果安全要求更高，可以增加：

- Windows Named Pipe
- Linux/macOS Unix Domain Socket

但第一版建议先用 HTTP + WebSocket，方便 Web UI、Qt UI、CLI 复用。

## 和 qBittorrent 的关系

qBittorrent 桌面版通常是 Qt GUI 和核心逻辑在同一进程里。

`qbittorrent-nox` 更接近本项目参考：

- 无 GUI 运行
- 内置 HTTP server
- 提供 Web UI 和 Web API
- 浏览器通过 HTTP API 控制后端

本项目建议从一开始就明确 daemon API，让 Web UI、Qt UI、CLI 复用同一协议。

对应关系：

```text
qbittorrent-nox       -> runbayd
qBittorrent Web UI    -> RunBay Web UI
libtorrent            -> RunBay process supervisor / scheduler
qBittorrent Web API   -> RunBay REST + WebSocket API
```

## 任务配置示例

任务配置不要只存在 UI 里。建议用 SQLite 管状态，同时支持 YAML/TOML 导入导出。

```yaml
name: backup-db
command: python backup.py
cwd: /opt/jobs/backup
env:
  NODE_ENV: production
restart: on-failure
schedule: "0 3 * * *"
log_retention_days: 14
```

## 任务状态模型

任务状态不要只看进程是否存在，建议至少包含：

- `starting`
- `running`
- `stopping`
- `exited`
- `failed`
- `scheduled`
- `disabled`

## 自启设计

自启需要作为一等功能设计：

- Windows：安装为 Windows Service，支持 LocalSystem 或指定用户
- Linux：生成 systemd unit
- macOS：生成 LaunchDaemon，用户级则 LaunchAgent
- 无 UI 环境：`runbayctl` 必须完整可用
- Web UI：默认只监听 `127.0.0.1`
- 远程访问：必须显式开启认证

## 发布体积预期

Go daemon + SQLite + WebSocket + 内嵌 Web UI：

- Windows/Linux/macOS 单二进制大约 20-60 MB
- 如果做一个多命令二进制 `runbay`，预计 25-70 MB

可选命令形态：

```bash
runbay daemon
runbay ctl start backup-db
runbay ctl logs backup-db -f
runbay install-service
```

或者拆成：

```text
runbayd.exe
runbayctl.exe
```

## 重要设计原则

- UI 不负责长期进程生命周期
- daemon 是核心
- CLI 必须完整可用
- Web UI 第一版优先于 Qt
- Qt 只是可选客户端
- 默认只监听 localhost
- 远程访问必须显式开启认证
- 任务配置支持导入导出，例如 YAML/TOML
- 实时输出用 WebSocket
- 历史日志走分页 API
- 日志需要支持保留策略和滚动
