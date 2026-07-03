# Kirikiri SDL2 Web 游戏库 — 部署指南

把 Kirikiri SDL2 的 Emscripten/WASM 构建部署成一个**浏览器游戏库**：主页列出所有 `.xp3` 游戏，点击即玩，元数据可在主页直接编辑保存。

## 文件说明

| 文件 | 作用 |
|---|---|
| `tools/server.py` | 静态托管 + 游戏库 API（扫描/元数据/封面上传），仅用 Python 标准库 |
| `src/config/home.html.in` | 游戏库主页模板（CMake 生成 `index.html`） |
| `src/config/play.html.in` | 游戏运行页模板（CMake 生成 `play.html`），基于原 `index.html.in` 改造 |
| `games/` | 放 `.xp3` 游戏包的目录 |
| `covers/` | 封面图目录（主页上传或手动放入） |
| `manifest.json` | 元数据存储（服务端自动创建维护，无需手写） |

> `src/config/index.html.in` 是改造前的原始模板，已被 `play.html.in` 取代，保留仅作参考，不参与构建。

## 一、构建 WASM

需要 [emsdk](https://github.com/emscripten-core/emsdk) 3.1.24+：

```bash
emcmake cmake -B build -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build
```

构建产物（在 `build/` 下）：
- `krkrsdl2.js` / `krkrsdl2.wasm` — 引擎本体
- `index.html` — 游戏库主页（由 `home.html.in` 生成）
- `play.html` — 游戏运行页（由 `play.html.in` 生成）
- `krkrsdl2.wasm.js` — 仅当 `-DKRKRSDL2_EMSCRIPTEN_ENABLE_JS_FALLBACK=ON` 时生成

## 二、准备部署目录

```bash
mkdir deploy
cp build/krkrsdl2.js build/krkrsdl2.wasm build/index.html build/play.html deploy/
# 如启用了 JS fallback，也拷上：
# cp build/krkrsdl2.wasm.js deploy/
mkdir deploy/games deploy/covers
```

## 三、放入游戏并启动

```bash
# 把 .xp3 文件放进 games/
cp /path/to/your_game.xp3 deploy/games/

# 启动服务（默认端口 8080）
python tools/server.py deploy/ 8080
```

浏览器访问 `http://localhost:8080/`，主页会自动扫描 `games/` 并列出所有游戏。

## 四、编辑游戏信息

在主页：
1. 鼠标悬停游戏卡片，点右上角**编辑**。
2. 填写标题、简介、作者、**系列**（可选，留空归未分组），可上传封面图（png/jpg/webp）。
3. 点**保存**，元数据写入 `manifest.json`，刷新仍在。
4. 点**扫描游戏**按钮可重新扫描 `games/`（放入新游戏后点此刷新列表）。

## 五、日常使用

- **新增游戏**：把 `.xp3` 放进 `games/` → 主页点"扫描游戏" → 出现新卡片 → 编辑元数据保存。
- **存档**：游戏内存档通过 IndexedDB 持久化，刷新页面不丢（每个游戏独立存档空间）。
- **子目录**：`games/` 下可建子目录分类，扫描会递归发现。

## 服务端 API（供参考/二次开发）

| 方法 路径 | 行为 |
|---|---|
| `GET /api/games` | 实时列 `games/*.xp3`，返回 `[{file, size}]` |
| `GET /api/manifest` | 读 `manifest.json`（不存在返回 `{"games":[]}`） |
| `POST /api/manifest` | body `{games:[...]}`，原子写回 `manifest.json` |
| `POST /api/cover` | `application/octet-stream` + `X-Filename` 头，存入 `covers/`，返回 `{cover}` |
| 其他 | 静态文件托管（`index.html`、`play.html`、wasm、games/、covers/） |

`manifest.json` 格式：
```json
{
  "games": [
    {
      "file": "games/your_game.xp3",
      "title": "游戏标题",
      "description": "简介",
      "cover": "covers/your_game.png",
      "author": "作者",
      "series": "第一章",
      "size": 12345678
    }
  ]
}
```

## 系列

在编辑模态框填写**系列**字段即可把游戏归入系列（同名 = 同系列，留空归"未分组"）。

- **主页**：系列以汇总卡片展示（系列名、游戏数、封面拼贴），点卡片进入该系列的游戏列表；未填系列的游戏显示在"未分组"区。搜索时跨所有游戏扁平匹配（含系列名）。
- **游玩页**：当前游戏所属系列有 2 款及以上时，左上角"返回游戏库"旁会出现系列下拉，可快捷切换同系列其他游戏；否则隐藏。

## 安全提示

- `server.py` **无鉴权**，仅适合本地或可信内网，**请勿裸暴露到公网**。如需公网访问，加反向代理 + 鉴权。
- 运行页 `play.html` 的 `?data=` 参数有路径校验：允许 `games/xxx.xp3` 这类相对路径，拒绝 `..`、`//`、绝对 URL，防止路径穿越。
- 封面上传校验文件名（仅字母数字/点/下划线/连字符 + png/jpg/webp 扩展名）。

## 多线程构建（可选）

默认非线程构建，无需特殊 HTTP 头。若启用线程：

```bash
emcmake cmake -B build -DKRKRSDL2_EMSCRIPTEN_ENABLE_THREADS=ON
```

需要服务端发送 COOP/COEP 头（`server.py` 当前未发送，需自行扩展或用 Nginx）：

```nginx
add_header Cross-Origin-Opener-Policy "same-origin";
add_header Cross-Origin-Embedder-Policy "require-corp";
```

## 故障排查

- **主页空白 / "未发现游戏"**：确认 `games/` 里有 `.xp3`，点"扫描游戏"；看浏览器控制台有无 `/api/games` 报错。
- **点游戏后下载失败**：确认 `?data=` 指向的路径相对部署根正确；`play.html` 与 `krkrsdl2.js`/`.wasm` 在同一目录。
- **元数据保存后没更新**：刷新页面；检查 `manifest.json` 是否写入成功（看 `POST /api/manifest` 响应）。
- **封面不显示**：检查 `covers/` 里文件是否存在；封面路径在 `manifest.json` 里应为 `covers/xxx.png` 形式。

## 视频播放（VideoOverlay）

Web 构建通过 HTML5 `<video>` 元素实现 `VideoOverlay` 覆盖式播放：引擎从 `.xp3` 里读出视频字节，生成 Blob URL，把 `<video>` 叠加在画布上方按游戏坐标（含 letterbox）定位，播放结束后通过 `onStatusChanged` 回调通知脚本，因此依赖播放结束的脚本不会卡死。

- **支持格式**：浏览器原生能解码的容器/编码，主要是 `.mp4`(H.264+AAC)、`.webm`、`.ogv`(Theora)。`.mkv`/`.mov` 视浏览器而定。
- **不支持格式（wmv/avi 等）**：浏览器无法解码的格式不再直接读 xp3，而是引擎向 `video_cache/<gamePath>.d/<videoPath>.mp4` 请求预先转码好的 mp4；命中则用 `<video>` 播放，未命中（404）则记日志并跳过，游戏继续。生成缓存的方法见文末「为不支持的视频格式生成转码缓存」。
- **音频**：依赖浏览器的自动播放策略；游戏运行时玩家已有点击交互，通常可带声播放。若被策略拦截，会自动以静音方式继续播放。
- **模式**：`vomOverlay`/`vomMixer`/`vomMFEVR` 按设定的矩形覆盖显示；`vomLayer` 目前按覆盖方式尽力显示（不逐帧写入 Layer，因为 WASM 内解码代价过高）。
- **定位**：`<video>` 用 `position:absolute` 叠在 `#canvas` 上，依据画布逻辑尺寸与可视区域做等比缩放，窗口缩放时自动重定位。

## 为不支持的视频格式生成转码缓存

浏览器无法解码 `.wmv`/`.avi` 等格式，又不想重新转码后打包回 `.xp3`。本方案**在性能较强的机器上转码一次**，把 mp4 放到引擎会去请求的缓存目录；NAS 只需静态托管该目录，零实时转码、不重新打包 `.xp3`。

### 缓存路径约定

引擎按以下规则查找缓存（与 `src/core/visual/sdl2/VideoOvlImpl.cpp` 的 `TVP_EmscCacheRelPath` 一致）：

```
video_cache/<gamePath>.d/<videoPath>.mp4
```

- `<gamePath>` = 浏览器地址栏 `?data=` 的值，即 `.xp3` 相对 Web 根目录的路径，如 `games/Data.xp3`。
- `<videoPath>` = 视频在 `.xp3` 内的路径，如 `video/spp1.wmv`，保留原始文件名（含扩展名）再追加 `.mp4`。

例：`?data=games/Data.xp3` 里的 `video/spp1.wmv` → `video_cache/games/Data.xp3.d/video/spp1.wmv.mp4`。

### 生成步骤

1. 安装 [ffmpeg](https://ffmpeg.org/)，确认 `ffmpeg -version` 可用。
2. 在仓库根目录运行（`<webroot>` 为部署/测试根目录，如 `.test-deploy` 或 `deploy`）：

   ```bash
   python scripts/transcode_videos.py <webroot>/games/Data.xp3 --out-root <webroot>
   ```

   脚本会解析 xp3，挑出浏览器不支持的格式（wmv/asf/avi/mpg/mpeg/mpe/flv/rm/rmvb/vob/3gp），逐个用 ffmpeg 转码为 H.264+AAC、`+faststart` 的 mp4，写到 `<webroot>/video_cache/...`；已存在则跳过，`--force` 覆盖。

3. 常用选项：

   | 选项 | 作用 |
   |---|---|
   | `--out-root DIR` | Web 根目录，缓存写到其下 `video_cache/`（默认当前目录） |
   | `--game-path PATH` | 覆盖 `gamePath`（即 `?data=` 值），默认取 xp3 相对 `--out-root` 的路径 |
   | `--ffmpeg PATH` | 指定 ffmpeg 可执行文件路径 |
   | `--preset veryfast` | libx264 预设（越慢体积越小，默认 `veryfast`） |
   | `--crf 23` | 画质（越小越好，默认 23） |
   | `--force` | 覆盖已存在的缓存 |
   | `--dry-run` | 只列出视频与目标路径，不转码 |
   | `--list` | 只列出 xp3 内需转码的视频 |

4. 把生成的 `video_cache/` 整个目录连同游戏一起放到 NAS 部署目录（与 `games/` 同级）。`tools/server.py` 已能静态托管并支持 Range 请求，无需改服务端。

### 验证

- 浏览器控制台会打印 `[tvpVideo]` 相关日志；命中缓存时 `<video>` 正常播放，结束触发 `onStatusChanged`。
- 缓存缺失时引擎收到 404 后记日志并跳过该视频，游戏不卡死。
- 可先 `--dry-run` 确认目标路径与 `?data=` 一致再正式转码。

### 说明

- 转码只需在性能较强的机器上做一次；弱性能 NAS 只负责静态托管。
- 支持格式（mp4/webm 等）仍直接从 `.xp3` 读字节播放，无需缓存。
- 脚本自带 xp3 解析，不依赖仓库内其它工具；xp3 带 MZ(PE) 头也能识别。