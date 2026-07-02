# krkrsdl2 Web DLL Plugin Compatibility Execution Steps

本文档是 `plugin-compatibility-upgrade-checklist.md` 的执行版。目标是把“国内游戏常见 DLL 插件兼容”拆成可以逐步落地的工程步骤。

核心原则：

- 原游戏脚本尽量不改，继续允许 `Plugins.link("xxx.dll")`。
- Web 端不执行 Windows `.dll` 二进制。
- Web 端通过 WebAssembly side module、内置 Web 等价插件、或降级 shim 兼容插件行为。
- Android 端的思路是参考对象：脚本写 `.dll`，非 Windows 平台实际加载 `.so`。

## 0. 准备工作

### 0.1 确认当前仓库状态

在项目根目录执行：

```powershell
git status --short
```

预期：

- 只看到你明确新增或修改的文档、代码。
- 如果出现不认识的代码改动，先确认来源，不要直接覆盖。

### 0.2 确认子模块完整

```powershell
git submodule status
```

如果子模块缺失，执行：

```powershell
git submodule update --init --recursive
```

需要重点确认：

- `external/krkrz`
- `external/SDL`
- `external/zlib`
- `external/FAudio`

### 0.3 确认 Emscripten 环境

```powershell
emcc --version
em++ --version
emcmake --version
```

预期：

- 三个命令都能正常输出版本。
- 后续所有主程序和插件 side module 必须使用同一个 emsdk 版本编译。

## 1. 建立插件目录结构

### 1.1 新增目录

```powershell
New-Item -ItemType Directory -Force plugins
New-Item -ItemType Directory -Force plugins\src
New-Item -ItemType Directory -Force plugins\web
New-Item -ItemType Directory -Force plugins\tests
New-Item -ItemType Directory -Force plugins\shims
New-Item -ItemType Directory -Force plugins\third_party
```

目录用途：

- `plugins/manifest.json`: 插件兼容清单。
- `plugins/src`: 可编译插件源码适配层。
- `plugins/web`: Web 发布用插件产物。
- `plugins/tests`: 插件 TJS 测试脚本。
- `plugins/shims`: Web 等价实现或降级 shim。
- `plugins/third_party`: 从 GitHub 引入的插件源码。

### 1.2 新增基础 manifest

创建 `plugins/manifest.json`：

```json
{
  "schemaVersion": 1,
  "engine": "krkrsdl2-web",
  "plugins": []
}
```

验收：

```powershell
Get-Content plugins\manifest.json
```

## 2. 导入一期常用插件清单

一期不做全量 50+ 插件矩阵，只做国内游戏最常见、最影响启动和基础流程的插件。目标是先覆盖标题菜单、KAG 扩展、中文文本/字体、JSON/CSV 数据、文件状态、基础存档结构、常见转场和基础 Layer 图像处理。

### 2.1 使用内置 manifest 作为源头

当前一期清单文件：

```text
plugins/manifest.json
```

这个文件只列出一期要实现的 DLL，并为每个 DLL 指定：

- `dllName`: 原脚本中的 DLL 名称。
- `aliases`: 大小写或历史别名。
- `webModule`: Web 侧实际加载的模块。
- `sourceRepo`: 源码或来源线索。
- `implementationType`: 实现方式。
- `compatLevel`: 兼容等级。
- `priority`: 实施优先级。
- `exports`: 预期导出符号。
- `dependencies`: Web 实现依赖。
- `testScript`: 验收脚本。
- `implementation`: 具体实现策略。

### 2.2 一期 DLL 清单

一期必做：

```text
menu.dll
KAGParser.dll
json.dll
fstat.dll
wutcwf.dll
```

一期增强：

```text
addFont.dll
extrans.dll
csvParser.dll
saveStruct.dll / savestruct.dll
layerExImage.dll / LayerExImage.dll
```

其他插件暂时只放在 `deferredPlugins`，不作为一期实施入口。遇到真实游戏必须依赖时，再单独提升到一期补丁。

### 2.3 实现方式枚举

`implementationType` 必须使用以下类型之一：

```text
side-module
side-module-with-fs-adapter
side-module-with-text-adapter
side-module-or-shim
web-shim
```

含义：

- `side-module`: 直接把源码编译为 Emscripten side module。
- `side-module-with-fs-adapter`: 编译源码，但文件访问必须改走 Emscripten FS。
- `side-module-with-text-adapter`: 编译源码，但文本、编码、字体行为需要 Web 适配。
- `web-shim`: 不编译原 DLL，直接用 JS/DOM/Canvas/WebAudio/WebCodecs 实现同名脚本行为。

### 2.4 一期插件实现策略

Web shim 插件：

```text
menu.dll
addFont.dll
```

实现：

- `menu.dll`: 用 DOM 或 Canvas 实现 `MenuItem`、`Window.menu`、快捷键和点击回调，不移植 Win32 菜单。
- `addFont.dll`: 用 `FontFace` 从游戏包或 Emscripten FS 注册字体，不调用系统字体安装接口。

优先 side module 插件：

```text
KAGParser.dll
json.dll
csvParser.dll
saveStruct.dll
extrans.dll
layerExImage.dll
wutcwf.dll
```

实现：

- 拉取对应源码，编译为 Emscripten side module。
- 保持脚本继续写 `Plugins.link("xxx.dll")`。
- Web 实际加载 `/plugin/xxx.so`。
- 用 Windows krkrz 或 Android krkrsdl2 输出作为行为基准。

文件系统插件：

```text
fstat.dll
```

实现：

- 所有路径限定在 Emscripten FS、IDBFS、游戏包虚拟路径。
- 浏览器沙箱外本地文件、盘符、注册表一律不支持。
- 不可用字段返回明确 fallback 或抛出明确异常。

Layer、图像、Transition 插件：

```text
extrans.dll
layerExImage.dll
```

实现：

- 纯像素算法优先 side module。
- SSE/AVX 改为标量或 WebAssembly SIMD。
- DirectX/GDI 依赖不进入一期。

### 2.5 manifest 验收

执行：

```powershell
Get-Content plugins\manifest.json | ConvertFrom-Json | Select-Object schemaVersion, engine, compatibilityMode
```

执行：

```powershell
$m = Get-Content plugins\manifest.json | ConvertFrom-Json
$m.plugins | Select-Object dllName, implementationType, compatLevel, priority, status
```

预期：

- manifest 是合法 JSON。
- `compatibilityMode` 是 `phase1-common-cn`。
- `plugins` 数量保持在一期范围，不做 50+ 插件扩张。
- 每个插件都有 `dllName`。
- 每个插件都有 `implementationType`。
- 每个插件都有 `compatLevel`。
- 每个插件都有 `priority`。
- 每个插件都有 `implementation`。

## 3. 明确插件兼容等级

### 3.1 分类规则

逐个插件判断：

- `A`: 几乎纯 C/C++/TJS V2 插件，可以直接编译为 WebAssembly side module。
- `B`: 有少量平台 API，需要替换后编译。
- `C`: 依赖 Win32 UI、窗口、菜单、对话框，需要 Web 等价实现。
- `D`: 可以空实现或部分降级，不影响主流程。
- `E`: 无源码、强依赖系统能力、无法可靠兼容。

### 3.2 一期插件建议

优先处理：

```text
menu.dll
KAGParser.dll
json.dll
fstat.dll
wutcwf.dll
addFont.dll
```

处理顺序：

1. `menu.dll`: 先保证标题菜单和快捷键类脚本不崩。
2. `KAGParser.dll`: 验证 side module 插件链路。
3. `json.dll` / `fstat.dll`: 覆盖配置、数据和文件判断。
4. `wutcwf.dll` / `addFont.dll`: 覆盖中文文本、字体和编码问题。
5. `extrans.dll` / `csvParser.dll` / `saveStruct.dll` / `layerExImage.dll`: 作为一期增强补齐常见演出和数据能力。

## 4. 实现 Web 插件预加载流程

### 4.1 修改目标

启动 krkrsdl2 主 wasm 前，先把插件写入 Emscripten FS：

```text
/plugin/KAGParser.so
/plugin/menu.js
/plugin/json.so
```

原因：

- TJS 的 `Plugins.link` 是同步调用。
- 浏览器网络下载是异步操作。
- 所以必须启动前预加载，不能在 `Plugins.link` 内部临时下载。

### 4.2 新增 JS 预加载文件

创建 `src/config/plugin-preload.js`，职责：

- 读取 `plugins/manifest.json`
- 下载 `webModule`
- 写入 `Module.FS`
- 可选同步到 `IDBFS`
- 打印加载日志

伪代码：

```javascript
async function preloadKirikiriPlugins(Module, manifestUrl) {
  const manifest = await fetch(manifestUrl).then(r => r.json());
  Module.FS.mkdirTree("/plugin");

  for (const plugin of manifest.plugins) {
    if (plugin.implementationType !== "side-module") continue;

    const response = await fetch(plugin.webModule);
    if (!response.ok) {
      throw new Error(`Failed to fetch plugin ${plugin.dllName}: ${response.status}`);
    }

    const bytes = new Uint8Array(await response.arrayBuffer());
    const target = "/" + plugin.webModule.replace(/^\/+/, "");
    Module.FS.writeFile(target, bytes);
    console.log(`[krkrsdl2-plugin] ${plugin.dllName} -> ${target}`);
  }
}
```

实际实现时注意：

- 如果 `plugin.webModule` 是 `plugin/KAGParser.so`，写入路径必须是 `/plugin/KAGParser.so`。
- 需要在主模块开始运行前执行。
- 如果开启 IDBFS，需要 `FS.syncfs` 完成后再启动。

### 4.3 接入 HTML 模板

检查：

```powershell
Get-Content src\config\index.html.in
Get-Content src\config\home.html.in
Get-Content src\config\play.html.in
```

在实际使用的模板里加入：

```html
<script src="plugin-preload.js"></script>
```

并在 instantiate 前调用：

```javascript
await preloadKirikiriPlugins(Module, "plugins/manifest.json");
```

验收：

- 浏览器控制台能看到插件预加载日志。
- `/plugin` 目录内能看到写入的 `.so`。

## 5. 确认 `.dll -> .so` 映射

### 5.1 当前逻辑

现有逻辑在 `src/core/base/sdl2/StorageImpl.cpp`：

- 非 Windows 平台遇到 `xxx.dll`
- 自动改为 `xxx.so`
- 然后按插件搜索路径查找

### 5.2 Web 目标行为

在 Web 下：

```text
Plugins.link("KAGParser.dll")
```

应查找：

```text
/plugin/KAGParser.so
```

### 5.3 如果大小写不一致

国内游戏常见问题：

```text
Plugins.link("menu.dll")
Plugins.link("Menu.dll")
Plugins.link("MENU.DLL")
```

执行策略：

- 优先保留当前 `OPTION_ENABLE_CASEINSITIVITY` 行为。
- manifest 中使用原始 DLL 名称记录别名。
- 插件预加载时可额外写入小写 alias。

示例：

```json
{
  "dllName": "menu.dll",
  "aliases": ["Menu.dll", "MENU.DLL"],
  "webModule": "plugin/menu.js"
}
```

## 6. 建立 side module 构建链

### 6.1 新增 CMake 文件

创建 `plugins/CMakeLists.txt`：

```cmake
function(krkr_plugin_side_module target_name)
  add_library(${target_name} SHARED ${ARGN})

  set_target_properties(${target_name} PROPERTIES
    PREFIX ""
    SUFFIX ".so"
    CXX_STANDARD 14
    C_STANDARD 11
    POSITION_INDEPENDENT_CODE TRUE
  )

  target_compile_options(${target_name} PRIVATE
    -fPIC
    -sSIDE_MODULE=2
    -sDISABLE_EXCEPTION_CATCHING=0
  )

  target_link_options(${target_name} PRIVATE
    -sSIDE_MODULE=2
    -sDISABLE_EXCEPTION_CATCHING=0
  )

  target_include_directories(${target_name} PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/base/sdl2
    ${CMAKE_SOURCE_DIR}/external/krkrz/base
    ${CMAKE_SOURCE_DIR}/external/krkrz/tjs2
  )
endfunction()
```

注意：

- include 路径需要按实际插件源码报错调整。
- 如果插件需要更多 krkrz 头文件，逐个补，不要一开始加全量路径。

### 6.2 主 CMake 接入

在根目录 `CMakeLists.txt` 合适位置加入：

```cmake
if(${CMAKE_SYSTEM_NAME} STREQUAL "Emscripten")
  add_subdirectory(plugins)
endif()
```

### 6.3 构建命令

```powershell
New-Item -ItemType Directory -Force build-web
emcmake cmake -S . -B build-web -DOPTION_ENABLE_EXTERNAL_PLUGINS=ON
cmake --build build-web --config Release
```

验收：

- 主程序仍能产出 wasm/js/html。
- 插件目标能产出 `.so` side module。

## 7. 移植 `KAGParser.dll`

### 7.1 拉源码

```powershell
git clone https://github.com/krkrz/KAGParser plugins\third_party\KAGParser
```

如果不希望直接提交第三方源码：

- 改用 git submodule。
- 或在文档中登记版本和下载脚本。

### 7.2 看导出符号

搜索：

```powershell
rg -n "V2Link|V2Unlink|GetModuleInstance|TVPGetFunctionExporter" plugins\third_party\KAGParser
```

### 7.3 增加构建目标

在 `plugins/CMakeLists.txt` 中加入：

```cmake
krkr_plugin_side_module(KAGParser
  third_party/KAGParser/source-file-1.cpp
  third_party/KAGParser/source-file-2.cpp
)
```

实际文件名以源码仓库为准。

### 7.4 编译

```powershell
cmake --build build-web --target KAGParser --config Release
```

### 7.5 复制产物

```powershell
New-Item -ItemType Directory -Force plugins\web\plugin
Copy-Item build-web\plugins\KAGParser.so plugins\web\plugin\KAGParser.so -Force
```

### 7.6 新增测试

创建 `plugins/tests/KAGParser_smoke.tjs`：

```tjs
Plugins.link("KAGParser.dll");
Debug.message("KAGParser linked");
```

验收：

- Web 控制台看到 `KAGParser linked`。
- `Plugins.getList()` 包含 `KAGParser.dll` 或实际加载名。

### 7.7 已知阻塞：side-module stub 机制缺失

实际移植 KAGParser 时发现，krkrsdl2 移除了 krkrz 的插件 stub 机制：

- `src/core/base/sdl2/FuncStubs.h` 仅 1 行声明（`extern void TVPExportFunctions();`），未由 `makestub.py` 生成完整内容。
- 主引擎不调用 `TVPInitImportStub` / `TVPUninitImportStub`，FuncStubs.cpp 也不定义它们。
- 主引擎把所有 TVP 函数直接编进主 wasm，插件函数靠链接进主模块工作，没有跨模块函数表查询。

后果：side-module 是独立 wasm，无法直接访问主 wasm 的 `TVPGetScriptDispatch` 等符号。传统 krkrz 插件（KAGParser 的 V2Link 调 `TVPInitImportStub(exporter)` + `TVPGetScriptDispatch()` + `TVPCreateNativeClass_KAGParser()`）因此无法工作。`Minimal.so`（不调任何 TVP 函数）能加载，但无法注册 TJS 类。

已修的 KAGParser krkrz 版本漂移（保留）：

- `TJS_NCM_REG_THIS` 由 `this` 改 `classobj`（工厂函数无 `this`，有局部 `classobj`）。
- 删除 `TVPInternalError` 旧式 `const tjs_char*` 定义（新 krkrz 改为 `tTJSMessageHolder`，冲突）。
- 覆盖 `TJS_NATIVE_SET_ClassID` 用 `TJS_NATIVE_CLASSID_NAME`（非 `__TP_STUB_H__` 下硬编码 `ClassID`，工厂函数无此成员）。

剩余阻塞错误：`TVPInitImportStub` / `TVPUninitImportStub` / `TVPPluginGlobalRefCount` 未声明。

解除阻塞的方向（未实施）：

1. 运行 `src/core/base/sdl2/makestub.py` 重新生成完整 `FuncStubs.h` / `FuncStubs.cpp`（含 `TVPInitImportStub` 实现和所有导出函数的 import stub）。
2. 让 side-module 编译时包含生成的 FuncStubs.cpp（或其 side-module 专用子集）。
3. 验证 side-module 通过 `exporter` 查表能调到主 wasm 的 `TVPGetScriptDispatch` 等。

注意：重建 stub 机制是 krkrsdl2 的架构级改动，可能影响主引擎构建，需单独评估。在此完成前，所有依赖 TVP 导出函数的传统 krkrz 插件 side-module 均无法工作；应优先用 web-shim 路径绕过（见 §8、§9）。

## 8. 实现 `menu.dll` Web 等价插件

### 8.1 拉源码作接口参考

```powershell
git clone https://github.com/krkrz/menu plugins\third_party\menu
```

### 8.2 提取脚本接口

搜索：

```powershell
rg -n "MenuItem|Window|menu|caption|checked|enabled|shortcut|onClick|V2Link" plugins\third_party\menu
```

整理 `menu.dll` 对脚本暴露的能力：

```text
类/对象 | 属性 | 方法 | 事件 | 是否必须
```

### 8.3 不直接移植 Win32

不要移植：

- `HMENU`
- `HWND`
- Win32 message loop
- `AppendMenu`
- `SetMenu`
- 系统原生菜单栏

Web 替代：

- DOM 菜单栏
- Canvas 覆盖菜单
- 键盘快捷键监听
- TJS 事件回调

### 8.4 新增 shim 文件

创建 `plugins/shims/menu.js`，职责：

- 注册 `MenuItem` 等同名对象。
- 给 `Window` 增加 `menu` 属性或等价入口。
- 将点击和快捷键回调到 TJS。

需要通过现有 `KirikiriEmscriptenInterface` 调用：

```javascript
Module.evalTJS("...");
```

### 8.5 manifest 标注

```json
{
  "dllName": "menu.dll",
  "webModule": "plugin/menu.js",
  "sourceRepo": "https://github.com/krkrz/menu",
  "implementationType": "web-shim",
  "compatLevel": "C",
  "status": "in-progress"
}
```

### 8.6 预加载 shim

在插件预加载逻辑中：

- `side-module`: 写入 FS。
- `web-shim`: 通过 `<script>` 或 dynamic import 加载。

验收：

- `Plugins.link("menu.dll")` 不报错。
- 游戏标题菜单能显示。
- 点击菜单项能触发原脚本回调。
- 快捷键能触发原脚本回调。

## 9. 处理纯功能插件

### 9.1 `json.dll`

优先级：

1. 有源码则编译 side module。
2. 没有源码则用浏览器 `JSON.parse` / `JSON.stringify` 做 Web shim。

测试：

```tjs
Plugins.link("json.dll");
Debug.message("json linked");
```

还需要真实解析：

```tjs
var text = "{\"a\":1}";
// 调用插件实际 API，确认返回结构
```

### 9.2 `fstat.dll`

Web 限制：

- 不能读取浏览器沙箱外文件。
- 不能返回 Windows 原生文件属性。

策略：

- 对 Emscripten FS 内存在的文件返回 size、mtime 等可用信息。
- 不可用字段返回 0、空值或抛出明确异常。

### 9.3 `sqlite3.dll`

策略：

- 优先源码编译 SQLite wasm side module。
- 数据文件放入 Emscripten FS。
- 存档类数据库同步到 IDBFS。

验收：

- 创建数据库。
- 写入一条记录。
- 刷新页面。
- 重新读取记录仍存在。

## 10. 处理图像和 Layer 插件

### 10.1 识别 bitmap 访问方式

搜索插件源码：

```powershell
rg -n "mainImageBuffer|bufferForWrite|Layer|pitch|width|height|tjs_uint8" plugins\third_party
```

### 10.2 可移植策略

- 如果插件是纯像素算法，编译为 side module。
- 如果插件依赖 SSE/AVX，替换为标量实现或 WebAssembly SIMD。
- 如果插件依赖 GDI/DirectX，改为 Canvas/WebGL 等价实现。

### 10.3 验收

- 对同一张输入图，Windows 和 Web 输出像素 hash 一致或视觉一致。
- 大图处理不会阻塞过久。
- 不越界访问 wasm memory。

## 11. 修改 `Plugins.link` 的 Web shim 分流

### 11.1 目标

当前 `Plugins.link` 只会走 native plugin 加载。为了支持 `menu.dll` 这种 Web shim，需要加一层分流：

```text
Plugins.link("xxx.dll")
  -> manifest 查找
  -> side-module: 继续 SDL_LoadObject
  -> web-shim: 调用 JS 注册逻辑，然后把插件记入列表
```

### 11.2 建议实现位置

候选文件：

- `src/core/base/sdl2/PluginImpl.cpp`
- `src/plugins/kremscripten.cpp`

建议：

- `PluginImpl.cpp` 保持插件语义入口。
- Emscripten 专用 JS 调用封装放在 `kremscripten.cpp` 或新增 `WebPluginShim.cpp`。

### 11.3 需要新增的 C++ 能力

在 Emscripten 下提供：

```cpp
bool TVPHasWebPluginShim(const ttstr &name);
void TVPLoadWebPluginShim(const ttstr &name);
void TVPUnloadWebPluginShim(const ttstr &name);
```

### 11.4 JS 侧能力

在 Module 上提供：

```javascript
Module.krkrPluginShim = {
  has(name) {},
  load(name) {},
  unload(name) {}
};
```

### 11.5 验收

- `Plugins.link("menu.dll")` 走 Web shim。
- `Plugins.link("KAGParser.dll")` 走 side module。
- `Plugins.getList()` 同时显示两类插件。
- `Plugins.unlink(...)` 能正确卸载或至少标记卸载。

## 12. 错误诊断

### 12.1 必须区分的错误

实现日志时必须区分：

- manifest 里没有该插件。
- manifest 有记录但产物没有下载。
- 产物已写入 FS 但 `SDL_LoadObject` 失败。
- side module 缺少 `V2Link`。
- side module 缺少 `GetModuleInstance`。
- Web shim 加载失败。
- 插件明确不支持 Web。

### 12.2 日志格式

建议统一：

```text
[krkrsdl2-plugin] request=menu.dll type=web-shim status=loaded module=plugin/menu.js
[krkrsdl2-plugin] request=KAGParser.dll type=side-module status=loaded module=/plugin/KAGParser.so
[krkrsdl2-plugin] request=foo.dll status=missing reason=not-in-manifest
```

## 13. 回归测试流程

### 13.1 单插件 smoke test

每个插件都要有：

```text
plugins/tests/<plugin>_smoke.tjs
```

最小内容：

```tjs
Plugins.link("<plugin>.dll");
Debug.message("<plugin> linked");
```

### 13.2 真实游戏流程测试

每个目标游戏至少跑：

- 启动。
- 标题画面。
- 菜单点击。
- 进入正文。
- 存档。
- 读档。
- 返回标题。

### 13.3 对照平台

同一测试脚本在三端对照：

- Windows krkrz。
- Android krkrsdl2。
- Web krkrsdl2。

记录：

```text
游戏名 | 插件 | Windows 结果 | Android 结果 | Web 结果 | 差异 | 处理方式
```

## 14. 发布流程

### 14.1 构建

```powershell
Remove-Item -Recurse -Force build-web -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force build-web
emcmake cmake -S . -B build-web -DOPTION_ENABLE_EXTERNAL_PLUGINS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-web --config Release
```

### 14.2 准备发布目录

```powershell
New-Item -ItemType Directory -Force dist
New-Item -ItemType Directory -Force dist\plugins
Copy-Item build-web\*.html dist\ -Force
Copy-Item build-web\*.js dist\ -Force
Copy-Item build-web\*.wasm dist\ -Force
Copy-Item plugins\manifest.json dist\plugins\manifest.json -Force
Copy-Item plugins\web\plugin dist\plugin -Recurse -Force
```

实际文件名以构建输出为准。

### 14.3 校验发布包

```powershell
Get-ChildItem dist -Recurse | Select-Object FullName, Length
```

必须包含：

- 主程序 `.wasm`
- 主程序 `.js`
- HTML
- `plugins/manifest.json`
- `/plugin/*.so`
- `/plugin/*.js`

## 15. 阶段验收门槛

### 15.1 第一阶段通过标准

- `plugins/manifest.json` 存在。
- 至少登记 `KAGParser.dll` 和 `menu.dll`。
- Web 启动前能预加载 side module 到 `/plugin`。
- `Plugins.link("KAGParser.dll")` 能尝试加载 `/plugin/KAGParser.so`。

### 15.2 第二阶段通过标准

- `KAGParser.dll` side module 样板跑通。
- `menu.dll` Web shim 可以被 `Plugins.link("menu.dll")` 调用。
- `Plugins.getList()` 能显示 side module 和 Web shim 两类插件。

### 15.3 第三阶段通过标准

- 一期插件全部有实现或明确降级。
- 一个真实游戏能完成启动、菜单、正文、存档、读档。
- 所有不可支持能力都有清晰错误，而不是静默失败。

## 16. 风险处理

### 16.1 插件无源码

处理：

- 先确认脚本实际使用了哪些 API。
- 只实现脚本调用到的行为。
- manifest 标记 `implementationType: "web-shim"`。
- 文档记录不是完整插件实现。

### 16.2 插件源码强依赖 Win32

处理：

- 不把 Win32 API 直接搬到 Web。
- 抽脚本接口。
- 用 DOM、Canvas、WebAudio、WebCodecs 或 Emscripten FS 替代。

### 16.3 插件需要同步等待浏览器异步能力

处理：

- 尽量启动前预加载。
- 字体、音频解码、视频资源等需要异步准备的能力，必须在游戏启动前或场景切换前完成。
- 不能在 TJS 同步调用里强行等待 Promise。

### 16.4 插件依赖线程

处理：

- 优先改为单线程。
- 如必须启用 pthread，需要全站 COOP/COEP header。
- 启用线程会影响部署要求，必须单独评估。

## 17. 推荐开发顺序

严格按以下顺序执行：

1. 建目录和 manifest。
2. 导入并维护一期常用插件 manifest。
3. 先做一期必做，再做一期增强。
4. 实现插件预加载。
5. 确认 `.dll -> .so` 路径在 Web 下命中。
6. 建 side module CMake helper。
7. 做 `menu.dll` Web shim。
8. 跑通 `KAGParser.dll` side module。
9. 补 `json/fstat/wutcwf/addFont`。
10. 补 `extrans/csvParser/saveStruct/layerExImage`。
11. 加 `Plugins.getList`、`unlink`、错误日志一致性。
12. 跑真实游戏回归。
13. 做发布缓存和版本校验。

## 18. 真实游戏验证进展（2026-07-02）

用户提供真实游戏 `D:/新建文件夹/Data.xp3`（477MB，519 文件）用于验证。

### 18.1 已验证通过

1. **引擎在 Web 端能跑起来**：XP3 成功加载（`Done. contains 519 file(s)`），TJS 引擎实例化，`Module.evalTJS('1+1')` 返回 2，TJS 可用。
2. **预加载链路在真实引擎工作**：console 日志显示 4 个 web-shim（menu/json/fstat/addFont）`status=loaded`，Minimal.so side-module `status=loaded`，其余 side-module 404（预期，stub 阻塞）。`manifest loaded: 11 plugin(s)`、`preload complete`。
3. **诊断日志格式**：预加载器 JS 日志格式正确（§12.2）。

### 18.2 当前卡点

引擎加载 XP3 后执行 `startup.tjs`，在"Done 519 files"后**静默 hang**（无后续日志、canvas 保持 300x150 默认尺寸、statusbar 隐藏）。关键观察：

- `Plugins.getList()` 返回 **0 个插件** —— 说明 startup.tjs 执行到 Plugins.link 之前就 hang 或异常终止了，web-shim 的 install（C++ 分流调 evalTJS）从未被触发。
- console 里**没有 C++ 分流日志**（`[krkrsdl2-plugin] request=... status=loading/loaded install-called`）——印证 Plugins.link 未被调用。
- `Storages.readText("startup.tjs")` 报 `Member "readText" does not exist` —— **krkrsdl2 的 Storages 没有 readText 方法**，这与 json.js / fstat.js shim 里假设的 `Storages.readText`/`Storages.writeText` 不符，是 shim 的一个已知错误（见 §18.3）。

### 18.3 待排查项（后续继续）

1. **startup.tjs 内容**：需提取查看（xp3 在 `D:/新建文件夹/Data.xp3`，需 XP3 解包工具，或用引擎的 `Storages.readText` 替代方法——但该方法不存在）。startup.tjs 可能在 Plugins.link 之前就因其他原因 hang（如等待字体、缺某个 side-module 抛异常、或 KAG 系统脚本问题）。
2. **C++ 分流日志未捕获**：我加的 `fprintf(stderr)` 日志在 preview console_logs（只抓 console.log）里没出现。emscripten stderr 默认到 console.error，preview 工具可能不抓 error 级别。验证时需用浏览器 DevTools 直接看 console.error，或改用 `Module.print`/`Debug.message` 输出。
3. **shim 的 TJS 语义错误**（未验证，但已发现一个）：
   - `Storages.readText` / `Storages.writeText` 在 krkrsdl2 不存在（json.js/fstat.js 假设错误）。需改用引擎实际提供的方法（可能是 `Storages.openText` / TJS 的 `Storages` 类 API 需核对 krkrsdl2 实现）。
   - menu.js 的 `Object.defineProperty(Global.Window, ...)` TJS 可能不支持。
   - `class MenuItem {...}` TJS 语法、`new Dictionary(...)`/`new Array(...)` 构造、`Global.Window` 可达性均未验证。
4. **web-shim install 从未被调用**：因为 Plugins.link 没被调用。需先解决 startup.tjs hang 的问题，才能验证 web-shim install。

### 18.4 继续工作的起点

1. 用浏览器 DevTools 打开 `http://localhost:8099/play.html?data=games/Data.xp3`，看 console.error 级别日志和异常堆栈，定位 startup.tjs hang 的根因。
2. 解包 Data.xp3（用 GARbro 或 krkrrel）提取 startup.tjs / system/Configure.tjs，确认 Plugins.link 调用集，核对 manifest 覆盖度（完成 task #22 扫描）。
3. 修正 shim 的 `Storages.readText`/`writeText` 假设，核对 krkrsdl2 的 Storages 实际 API（看 `external/krkrz/base/StorageIntf.cpp` 或 `src/core/base/sdl2/StorageImpl.cpp`）。
4. startup.tjs hang 解决后，web-shim install 才会被触发，届时验证 install 的 evalTJS 是否报错。

### 18.5 部署状态

`D:/新建文件夹/krkrsdl2_web/.test-deploy/` 已就绪（gitignore，不入库）：
- 最新 wasm（7789577 字节，含 step 10 诊断日志 + step 11 分流）
- `games/Data.xp3`（477MB，cp 自 `D:/新建文件夹/Data.xp3`）
- `plugins/shims/{json,fstat,menu,addFont}.js` + `plugins/manifest.json`
- `plugin/Minimal.so`

启动：`python tools/server.py .test-deploy 8099`，访问 `play.html?data=games/Data.xp3`。


