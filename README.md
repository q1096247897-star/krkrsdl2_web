# 吉里吉里 SDL2 / Kirikiri SDL2

## 项目说明 / About this fork

本项目 fork 自 [krkrsdl2/krkrsdl2](https://github.com/krkrsdl2/krkrsdl2)，在原项目基础上修改而来，主要新增了 **Emscripten/WASM 浏览器部署**（游戏库主页 + 游玩页 + Python 服务端）。

> **声明**：本仓库的修改部分由 AI（Claude）辅助完成。

- 上游原项目：https://github.com/krkrsdl2/krkrsdl2
- 本仓库：https://github.com/q1096247897-star/krkrsdl2_web
- 浏览器部署文档见 [web-deploy-README.md](web-deploy-README.md) 与 [docker/README.md](docker/README.md)

## 功能特性与支持情况

> 下表为本 fork（浏览器部署）相对上游 krkrsdl2 新增/改造的能力与完成度，上游 SDL2 桌面端能力不在本表内。

### 已完成

- **浏览器游戏库主页**：扫描 `games/*.xp3` 列出游戏卡片；元数据编辑（标题/简介/作者/系列/封面）；系列分组、作者分组、排序、搜索；卡片显示文件路径。
- **游玩页**：`?data=` 加载指定 xp3；逐游戏 IndexedDB 存档隔离（刷新不丢）；同系列快捷切换；界面中文化。
- **服务端** `tools/server.py`：静态托管 + 游戏库 API（游戏列表 / 元数据读写 / 封面上传），仅 Python 标准库；`?data=` 路径校验防穿越。
- **群晖 Docker 部署**：`docker/` 提供 Dockerfile + docker-compose；`scripts/prepare-deploy.*` 一键生成自包含部署包；`scripts/configure.sh` 交互配置端口/目录；数据卷持久化、开机自启。
- **视频播放（VideoOverlay）**：HTML5 `<video>` 覆盖式播放，按游戏坐标 + letterbox 定位，播放结束回调通知脚本。
- **音频播放**：Web Audio 输出；受浏览器自动播放策略拦截时自动静音续播。
- **插件兼容体系**：DLL 插件 Web 兼容框架（预加载 + side-module + web-shim + 扫描 + 诊断日志 + 发布包校验），`plugins/manifest.json` 统一描述。

### 视频格式支持

| 类型 | 格式 | 说明 |
|---|---|---|
| 直接播放 | `.mp4`(H.264+AAC)、`.webm`、`.ogv`(Theora) | 浏览器原生解码，直接从 xp3 读字节播放 |
| 视浏览器 | `.mkv`、`.mov` | 取决于浏览器解码能力 |
| 需转码缓存 | `.wmv`/`.asf`/`.avi`/`.mpg`/`.mpeg`/`.mpe`/`.flv`/`.rm`/`.rmvb`/`.vob`/`.3gp` | 浏览器无法解码；用 `scripts/transcode_videos.py` 预转码为 mp4 放到 `video_cache/`，引擎自动取缓存播放（弱 NAS 只需静态托管，无需实时转码、不重新打包 xp3） |
| 不支持 | DirectShow / MCI / Media Foundation 原生接口 | 浏览器环境无对应能力 |

### 插件兼容现状

Web 版复刻的是**插件行为**，不运行 Windows DLL。一期聚焦国内游戏最常见插件：

| 插件 | 类型 | 状态 |
|---|---|---|
| `Minimal.dll` | side-module（测试） | ✅ 已验证 |
| `layerExImage.dll` | side-module | 🔧 可构建，待真实游戏验证 |
| `json.dll` | web-shim | 🚧 进行中 |
| `fstat.dll` | web-shim | 🚧 进行中 |
| `menu.dll` | web-shim | 📋 计划中 |
| `addFont.dll` | web-shim | 📋 计划中 |
| `wutcwf.dll` | side-module | 📋 计划中 |
| `extrans.dll` | side-module | 📋 计划中 |
| `csvParser.dll` | side-module | 📋 计划中 |
| `saveStruct.dll` | side-module | 📋 计划中 |
| `KAGParser.dll` | side-module | ⛔ 阻塞（需重建 side-module stub 机制） |

另有 40+ 插件暂缓（`AlphaMovie`、`sqlite3`、`win32dialog`、`windowEx` 等，见 `plugins/manifest.json` 的 `deferredPlugins`）。完整计划见 [plugin-compatibility-upgrade-checklist.md](plugin-compatibility-upgrade-checklist.md)。

### 已知限制 / 不支持

- 不支持运行**未修改的商业游戏**（请用 Wine / Kirikiroid2）。
- 不加载原始 Windows `.dll` 二进制；依赖 Win32（HWND/GDI/COM/注册表）的插件只做等价或降级。
- 不支持浏览器沙箱外的本地文件访问（`C:\`、注册表、系统目录等）。
- `server.py` **无鉴权**，仅适合本地或可信内网，勿裸暴露公网。
- 视频转码缓存在 Linux NAS 上**区分大小写**：游戏脚本里的视频路径大小写需与 xp3 内实际一致，否则缓存 404（当前游戏均为小写，无影响）。

### 相关文档

- 部署与视频播放：[web-deploy-README.md](web-deploy-README.md)
- 群晖 Docker：[docker/README.md](docker/README.md)
- 插件兼容计划：[plugin-compatibility-upgrade-checklist.md](plugin-compatibility-upgrade-checklist.md)
- 插件实施步骤：[plugin-compatibility-execution-steps.md](plugin-compatibility-execution-steps.md)

---

吉里吉里 SDL2 是 [吉里吉里 Z](https://krkrz.github.io/) 的移植版，可在支持 [SDL2](https://www.libsdl.org/) 的平台上运行，例如 macOS、Linux，以及通过 Emscripten/WASM 在浏览器中运行。

详情请访问：https://krkrsdl2.github.io/krkrsdl2/

---

吉里吉里SDL2は、macOSやLinuxなど、[SDL2](https://www.libsdl.org/)をサポートするプラットフォームで実行できる[吉里吉里Z](https://krkrz.github.io/)の移植版です。

詳細については、次の Web ページを参照してください: https://krkrsdl2.github.io/krkrsdl2/

---

Kirikiri SDL2 is a port of [Kirikiri Z](https://krkrz.github.io/) that can be run on platforms supporting [SDL2](https://www.libsdl.org/), such as macOS and Linux.

Please visit the following webpage for more information: https://krkrsdl2.github.io/krkrsdl2/en/

## 运行商业游戏的注意事项 / 商用ゲームの実行に関する注意 / A note on running commercial games

本项目不支持运行未修改的商业游戏。请改用 [Wine](https://www.winehq.org/) 或 [Kirikiroid2](https://play.google.com/store/apps/details?id=org.tvp.kirikiri2)。

---

このプロジェクトを使用して変更されていない商用ゲームを実行することはサポートされていません。
代わりに[Wine](https://www.winehq.org/)または[Kirikiroid2](https://play.google.com/store/apps/details?id=org.tvp.kirikiri2)を使用してください。

---

Running unmodified commercial games using this project is not supported.
Please use [Wine](https://www.winehq.org/) or [Kirikiroid2](https://play.google.com/store/apps/details?id=org.tvp.kirikiri2) instead.

## 许可证 / ライセンス / License

吉里吉里 SDL2 源代码（`src` 目录内）在 MIT 许可证下授权。详情请阅读 `LICENSE`。
本项目包含第三方组件（未在 GPL 下授权），详情请参阅各组件的许可证文件。

---

吉里吉里SDL2ソース（`src`ディレクトリ内）のコードは、MITライセンスの下でライセンスされています。 詳細については、`LICENSE`をお読みください。
このプロジェクトには、サードパーティのコンポーネントが含まれています (GPL に基づいてライセンスされていません)。詳細については、各コンポーネントのライセンスファイルを参照してください。

---

The code of the Kirikiri SDL2 source (inside the `src` directory) is licensed under the MIT license. Please read `LICENSE` for more information.
This project contains third-party components (not licensed under the GPL). Please view the license file in each component for more information.
