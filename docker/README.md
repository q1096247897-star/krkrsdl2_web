# Kirikiri SDL2 Web — 群晖 Docker 部署

把游戏库部署到群晖 NAS，用 **Container Manager (Docker)** 跑 `server.py`，数据持久化在宿主目录，开机自启。

## 文件说明

| 文件 | 作用 |
|---|---|
| `docker/Dockerfile` | 镜像定义：python:3.12-slim + server.py + wasm 产物 |
| `docker/docker-compose.yml` | 容器编排：端口、卷挂载、重启策略 |
| `docker/.env.example` | 配置模板（端口、数据目录路径） |
| `docker/.dockerignore` | 构建时排除数据目录 |
| `scripts/prepare-deploy.ps1` | Windows：把 build 产物 + 模板组装成 `deploy/` |
| `scripts/prepare-deploy.sh` | Linux/Mac：同上 |
| `scripts/configure.sh` | 群晖上交互式修改端口/目录，生成 `.env` |

> `deploy/` 由 prepare 脚本生成，是自包含的部署包，已 gitignore。

## 部署架构

- **镜像内**：`server.py` + `krkrsdl2.js/.wasm` + `index.html/play.html`（这些每次发版才变）
- **宿主挂载**：`games/` `covers/` `manifest.json`（运行时数据，容器重建不丢）
- **端口**：宿主 `8080` → 容器 `8080`（可改）

---

## 完整步骤

### 一、本地构建 WASM 产物

群晖跑不了 Emscripten，必须在本地构建好。需要 [emsdk](https://github.com/emscripten-core/emsdk) 3.1.24+：

```bash
emcmake cmake -B build -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build
```

确认 `build/` 下有这四个文件（缺一不可）：
- `krkrsdl2.js` / `krkrsdl2.wasm`
- `index.html` / `play.html`

> 启用了 JS fallback 的话还会有 `krkrsdl2.wasm.js`，脚本会自动带上。

### 二、生成部署目录

在**项目根目录**运行（Windows 用 ps1，Mac/Linux 用 sh）：

```powershell
# Windows PowerShell
powershell -ExecutionPolicy Bypass -File scripts\prepare-deploy.ps1
```
```bash
# Linux / macOS
bash scripts/prepare-deploy.sh
```

脚本会创建 `deploy/`，结构如下：

```
deploy/
├── Dockerfile            # 镜像定义
├── docker-compose.yml    # 编排
├── .env.example          # 配置模板
├── .env                  # 配置（首次从 .env.example 复制）
├── .dockerignore
├── configure.sh          # 改目录/端口的脚本
├── server.py             # 服务端
├── krkrsdl2.js           # WASM 引擎
├── krkrsdl2.wasm
├── index.html            # 游戏库主页
├── play.html             # 游戏运行页
├── games/                # 放 .xp3（空）
├── covers/               # 封面（空）
└── manifest.json         # 元数据（初始 {"games":[]}）
```

### 三、上传到群晖

1. 用 **File Station** 把整个 `deploy/` 文件夹上传到群晖，比如 `/volume1/krkr-deploy/`
   - 或先打包成 zip 上传再解压（File Station 右键 → 解压）
2. 记下这个绝对路径，下一步要用

### 四、（可选）配置端口和数据目录

**方式 A — 交互式脚本（推荐）**：SSH 进群晖，进到部署目录运行：

```bash
cd /volume1/krkr-deploy
sh configure.sh
```

按提示输入端口和路径（直接回车用方括号里的默认值）。脚本会把结果写入 `.env`。

**方式 B — 手动改 `.env`**：直接编辑 `/volume1/krkr-deploy/.env`：

```env
PORT=8080
# 群晖上建议用绝对路径，数据集中管理
GAMES_DIR=/volume1/krkr-deploy/games
COVERS_DIR=/volume1/krkr-deploy/covers
MANIFEST_FILE=/volume1/krkr-deploy/manifest.json
```

> 不配置也能跑，默认数据就放在部署目录内的 `games/` `covers/` `manifest.json`。

### 五、启动容器

群晖需先装 **Container Manager** 套件（套件中心搜 "Container Manager"）。然后 SSH 进部署目录：

```bash
cd /volume1/krkr-deploy
docker compose up -d --build
```

`--build` 第一次会构建镜像（python:3.12-slim 拉取 + COPY 文件，约 1-2 分钟）。完成后：

- 访问 `http://群晖IP:8080/`
- 看到「未发现游戏」属正常，把 `.xp3` 传进 `games/` 后点主页「扫描游戏」

> 不想用命令行：Container Manager → **项目** → 新增 → 路径填 `/volume1/krkr-deploy` → 它会识别 `docker-compose.yml` → 启动。效果一样。

---

## 日常使用

| 操作 | 做法 |
|---|---|
| **加游戏** | File Station 把 `.xp3` 传进 `games/`（子目录也行）→ 主页点「扫描游戏」 |
| **改游戏信息** | 主页悬停卡片 → 编辑 → 填标题/简介/作者/系列/封面 → 保存 |
| **重启服务** | `docker compose restart` |
| **看日志** | `docker compose logs -f` |
| **停止** | `docker compose down` |

加游戏/封面**不用重启容器**——`server.py` 实时扫描 `games/`，数据通过卷挂载直通。

## 更新到新版本

本地重新构建 + 生成部署包后，只需替换群晖上的**产物文件**（不动 games/covers/manifest）：

1. 本地：`cmake --build build` → `prepare-deploy.ps1` 生成新 `deploy/`
2. 群晖：用 File Station 把新的 `server.py` `krkrsdl2.*` `*.html` 覆盖到 `/volume1/krkr-deploy/`
3. 重建镜像：`cd /volume1/krkr-deploy && docker compose up -d --build`

存档和元数据（manifest.json）不受影响。

## 安全提示

- `server.py` **无鉴权**，仅适合本地或可信内网。
- **不要**把 8080 端口直接映射到公网。如需外网访问：
  - 用群晖「控制面板 → 登录门户 → 反向代理」套一层，配合 DSM 账号认证
  - 或在 nginx 里加 Basic Auth
- `play.html` 的 `?data=` 已做路径校验（拒 `..`、绝对路径、`javascript:` 等 scheme）。

## 故障排查

- **容器起不来 / 端口占用**：`docker compose logs` 看报错；换 `.env` 里的 `PORT`。
- **manifest.json 写入失败 / 变成目录**：Docker 文件挂载要求宿主文件已存在。prepare 脚本已创建 `{"games":[]}`；若误删，手动 `printf '%s' '{"games":[]}' > manifest.json` 重建文件后 `docker compose restart`。
- **扫描不到游戏**：确认 `.xp3` 在 `GAMES_DIR` 指向的目录里；主页点「扫描游戏」；看 `docker compose logs` 有无 `/api/games` 报错。
- **路径权限**：群晖上确保运行容器的用户对 `games/` `covers/` `manifest.json` 有读写权限。Container Manager 默认 root 跑容器一般无碍。
- **wasm 加载失败**：确认 `krkrsdl2.js`/`.wasm`/`play.html`/`index.html` 都已上传且与 `server.py` 同目录（镜像内 `/app`）。
