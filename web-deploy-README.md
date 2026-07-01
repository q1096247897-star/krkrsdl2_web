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
