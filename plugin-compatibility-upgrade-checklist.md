# krkrsdl2 Web DLL Plugin Compatibility Upgrade Checklist

## 目标边界

- [ ] 保持游戏脚本侧兼容：原游戏中的 `Plugins.link("xxx.dll")`、`Storages.isExistentPlugin("xxx.dll")` 尽量不改。
- [ ] Web 端不加载 Windows PE `.dll` 二进制；所有兼容能力通过源码移植、WebAssembly side module、或 Web 等价实现完成。
- [ ] Android 兼容逻辑作为参考：非 Windows 平台把 `xxx.dll` 映射为 `xxx.so` 后通过动态链接加载。
- [ ] Web 兼容目标是复刻“同名插件行为”，不是复刻 Windows DLL 加载器。
- [ ] 每个插件必须能说明：源码来源、许可证、导出接口、平台依赖、Web 实现方式、测试覆盖。

## 当前代码依据

- [ ] `CMakeLists.txt` 已开启 `OPTION_ENABLE_EXTERNAL_PLUGINS`，`Plugins.link` 不是空实现。
- [ ] Emscripten 构建已在外部插件开启时加入 `-sMAIN_MODULE=1`，具备 main module + side module 基础。
- [ ] `src/core/base/sdl2/StorageImpl.cpp` 中非 Windows 平台已存在 `.dll -> .so` 的文件名映射逻辑。
- [ ] `src/core/base/sdl2/PluginImpl.cpp` 使用 `SDL_LoadObject` 加载插件，并读取 `V2Link`、`V2Unlink`、`GetModuleInstance`。
- [ ] `android-project/app/build.gradle` 通过 `jniLibs.srcDir 'libs'` 支持打包 Android native `.so` 插件。
- [ ] `src/plugins/kremscripten.cpp` 已提供 TJS 和 JS 互通能力，可用于 Web 等价插件或调试。

## Phase 0: 插件资产盘点

- [ ] 收集目标游戏实际使用的所有 DLL 名称。
- [ ] 按游戏包扫描 `Plugins.link(...)`、`Storages.isExistentPlugin(...)`、`System.exePath` 拼接加载等调用。
- [ ] 建立 `plugins/manifest.json` 设计稿，字段至少包含：
  - [ ] `dllName`
  - [ ] `webModule`
  - [ ] `sourceRepo`
  - [ ] `license`
  - [ ] `exports`
  - [ ] `dependencies`
  - [ ] `compatLevel`
  - [ ] `implementationType`
  - [ ] `testScript`
- [ ] 插件按类型分组：
  - [ ] TJS/V2 原生扩展插件
  - [ ] TSS 音频/媒体解码插件
  - [ ] 文件、压缩、JSON、SQLite 等纯库插件
  - [ ] Layer/Bitmap/Transition 视觉插件
  - [ ] Win32 UI、菜单、窗口、对话框插件
  - [ ] 视频、MCI、DirectShow、DirectX 相关插件
  - [ ] 厂商私有插件
- [ ] 每个插件标注兼容等级：
  - [ ] `A`: 可源码编译为 WebAssembly side module
  - [ ] `B`: 需要少量平台适配后可编译
  - [ ] `C`: 需要 Web 等价实现
  - [ ] `D`: 只能做空实现或降级实现
  - [ ] `E`: 不可可靠兼容

## Phase 1: Web 插件加载基础设施

- [ ] 明确 Web 插件产物命名规则。
  - [ ] 脚本请求 `menu.dll`
  - [ ] Web 查找 `/plugin/menu.so` 或 `/plugin/menu.wasm`
  - [ ] 优先保持 `.so` 后缀，以匹配当前非 Windows 映射逻辑和 Emscripten side module 习惯
- [ ] 修改或确认 `TVPLocatePlugin` 在 Emscripten 下的搜索路径。
  - [ ] 默认搜索 `${ORIGIN}`
  - [ ] 默认搜索 `${ORIGIN}/system`
  - [ ] 默认搜索 `${ORIGIN}/plugin`
  - [ ] 支持 `-krkrsdl2_pluginsearchpath`
  - [ ] 支持 `KRKRSDL2_PATH`
- [ ] 设计 Web 预加载流程。
  - [ ] 页面启动前读取插件 manifest
  - [ ] 下载插件 side module
  - [ ] 写入 Emscripten FS 的 `/plugin`
  - [ ] 可选同步到 IDBFS
  - [ ] 再启动 krkrsdl2 主模块
- [ ] 保持 `Plugins.link` 同步语义。
  - [ ] 不在 `Plugins.link` 内部做异步网络下载
  - [ ] 缺失插件时给出明确错误
  - [ ] 控制台打印实际映射路径
- [ ] 增加插件加载诊断日志。
  - [ ] 原始 DLL 名称
  - [ ] 映射后的 Web 模块路径
  - [ ] 是否命中 manifest
  - [ ] `SDL_LoadObject` 结果
  - [ ] `V2Link` / `GetModuleInstance` 是否存在

## Phase 2: Emscripten Side Module 构建链

- [ ] 新增插件构建目录，例如 `plugins-src/` 或 `external/plugins/`。
- [ ] 新增插件输出目录，例如 `web/plugins/` 或构建目录下的 `plugin/`。
- [ ] 新增 CMake helper，例如 `krkr_plugin_side_module(name sources...)`。
- [ ] 插件编译参数统一：
  - [ ] `em++`
  - [ ] `-shared`
  - [ ] `-fPIC`
  - [ ] `-sSIDE_MODULE=2`
  - [ ] 与主模块一致的异常、RTTI、C++ 标准和优化等级
- [ ] 主模块保留：
  - [ ] `-sMAIN_MODULE=1`
  - [ ] `-sFORCE_FILESYSTEM=1`
  - [ ] `-sALLOW_MEMORY_GROWTH=1`
- [ ] 梳理导出符号。
  - [ ] `V2Link`
  - [ ] `V2Unlink`
  - [ ] `GetModuleInstance`
  - [ ] 其他插件自定义导出
- [ ] 确认 `tp_stub` / `FuncStubs` 对 side module 可见。
- [ ] 产物生成后自动复制到 Web 可访问目录。
- [ ] 加入构建失败时的依赖提示，包括缺失头文件、Win32 API、汇编/SIMD、线程等。

## Phase 3: 首批插件移植顺序

- [ ] P0: `KAGParser.dll`
  - [ ] 拉取或登记 GitHub 源码
  - [ ] 确认是否只依赖 TJS/V2 接口
  - [ ] 编译为 side module
  - [ ] 验证 `Plugins.link("KAGParser.dll")`
  - [ ] 跑基础 KAG 解析脚本
- [ ] P0: `menu.dll`
  - [ ] 拉取或登记 krkrz/menu 源码
  - [ ] 拆出脚本可见 API
  - [ ] 标注 Win32 菜单依赖
  - [ ] 不直接移植 Win32 菜单
  - [ ] 用 DOM/Canvas/键盘事件实现 Web 等价菜单
  - [ ] 保持 `MenuItem`、`Window.menu` 等常用脚本接口兼容
- [ ] P0: `json.dll`
  - [ ] 优先用源码编译
  - [ ] 若源码不稳定，提供 TJS/JS 原生 JSON 等价实现
- [ ] P0: `fstat.dll`
  - [ ] 对齐 Web FS 可获得的文件状态
  - [ ] 浏览器不可获得的本地文件属性返回降级值
- [ ] P0: `extrans.dll`
  - [ ] 检查图像 transition 算法依赖
  - [ ] 优先 WebAssembly 编译
  - [ ] 无法编译的效果用 Canvas/WebGL 实现
- [ ] P0: `wutcwf.dll`
  - [ ] 确认文本编码、宽字符、字体相关行为
  - [ ] 浏览器字体不可控部分提供降级策略
- [ ] P1: `addFont.dll`
  - [ ] 映射到 CSS FontFace API
  - [ ] 支持从游戏包加载字体文件
  - [ ] 等待字体加载完成后再允许脚本继续使用
- [ ] P1: `sqlite3.dll`
  - [ ] 使用 SQLite wasm 或源码 side module
  - [ ] 数据库文件存入 Emscripten FS / IDBFS
  - [ ] 增加持久化同步测试
- [ ] P1: `minizip.dll` / 压缩类插件
  - [ ] 优先源码编译
  - [ ] 校验 XP3 以外压缩包读取行为
- [ ] P1: `layerEx*` / 图像处理插件
  - [ ] 确认是否直接操作 Layer bitmap buffer
  - [ ] 优先复用 `getLayerBitmapUInt8Array`
  - [ ] 性能敏感路径评估 WebAssembly SIMD

## Phase 4: Web 等价层设计

- [ ] 新增 Web 兼容插件框架。
  - [ ] 能注册与 DLL 同名的兼容插件
  - [ ] 能注册 TJS 全局类或对象
  - [ ] 能在 `Plugins.link` 时选择 side module 或内置实现
- [ ] UI 类插件等价策略：
  - [ ] `menu.dll`: DOM 菜单或 Canvas 菜单
  - [ ] `win32dialog.dll`: 浏览器 modal / file picker 降级
  - [ ] `windowEx.dll`: 只支持浏览器允许的窗口状态
  - [ ] `clipboard`: 使用 Clipboard API，权限失败时降级
- [ ] 音频类插件等价策略：
  - [ ] 优先 WebAudio / OpenAL 路线
  - [ ] 不支持 MCI/CD-DA
  - [ ] 不支持直接访问系统混音器
- [ ] 视频类插件等价策略：
  - [ ] 优先 HTMLVideoElement 或 WebCodecs
  - [ ] DirectShow、Media Foundation、MCI 不直接兼容
  - [ ] 映射常见播放、暂停、停止、seek、音量接口
- [ ] 文件系统类插件等价策略：
  - [ ] 只访问游戏包、Emscripten FS、IDBFS
  - [ ] 不暴露浏览器沙箱外本地路径
  - [ ] `C:\`、注册表、系统目录等返回不可用或虚拟值

## Phase 5: 脚本兼容和降级策略

- [ ] `Storages.isExistentPlugin("xxx.dll")` 应返回 manifest 和实际产物综合结果。
- [ ] 插件缺失时提供明确错误，不静默成功。
- [ ] 对已知可空实现插件提供 compatibility shim。
- [ ] 对危险或不可实现接口明确抛异常。
- [ ] 支持按游戏定制兼容配置。
  - [ ] 禁用某插件
  - [ ] 强制使用 Web 等价实现
  - [ ] 覆盖插件路径
  - [ ] 覆盖插件版本
- [ ] 记录所有降级行为到控制台和诊断面板。

## Phase 6: 测试体系

- [ ] 新建 `tests/plugins/`。
- [ ] 每个插件至少一个 TJS smoke test。
- [ ] 每个 P0 插件至少一个真实游戏片段回归测试。
- [ ] Windows krkrz 基准测试：
  - [ ] 加载插件
  - [ ] 运行测试脚本
  - [ ] 保存输出
- [ ] Android krkrsdl2 对照测试：
  - [ ] 加载 `.so`
  - [ ] 运行相同测试脚本
  - [ ] 保存输出
- [ ] Web krkrsdl2 测试：
  - [ ] 预加载 side module
  - [ ] 运行相同测试脚本
  - [ ] 比对输出
- [ ] 浏览器测试矩阵：
  - [ ] Chrome desktop
  - [ ] Edge desktop
  - [ ] Chrome Android
  - [ ] Safari iOS，如目标需要
- [ ] 测试指标：
  - [ ] 插件加载成功
  - [ ] 注册对象存在
  - [ ] 常用方法行为一致
  - [ ] 异常行为可预期
  - [ ] 存档和缓存可持久化
  - [ ] UI 不阻塞主循环
  - [ ] 音视频不会破坏用户手势播放限制

## Phase 7: 发布和缓存

- [ ] 插件 side module 单独产出并带 hash。
- [ ] manifest 中记录 hash 和版本。
- [ ] Service Worker 或应用缓存按 hash 缓存插件。
- [ ] 主模块和插件版本不匹配时拒绝加载。
- [ ] 支持清理旧插件缓存。
- [ ] 发布包包含：
  - [ ] 主 wasm
  - [ ] 主 JS glue
  - [ ] `index.html`
  - [ ] `plugins/manifest.json`
  - [ ] 插件 side modules
  - [ ] 兼容说明文档

## Phase 8: 不支持清单

- [ ] 原始 Windows `.dll` 二进制直接加载：不支持。
- [ ] 依赖 Win32 HWND、HMENU、GDI、COM、注册表的原生行为：只做等价或降级。
- [ ] DirectShow、DirectDraw、Direct3D、MCI 原生接口：不直接支持。
- [ ] 浏览器沙箱外文件访问：不支持。
- [ ] 任意 native 插件由玩家上传后直接执行：不支持。
- [ ] 没有源码且行为复杂的私有 DLL：只能通过逆向脚本调用面后做等价实现。

## 验收标准

- [ ] 原脚本 `Plugins.link("menu.dll")` 在 Web 下不需要改名。
- [ ] `Plugins.getList()` 能看到已加载插件名。
- [ ] `Storages.isExistentPlugin("xxx.dll")` 在 Web 下能反映真实可用性。
- [ ] P0 插件全部有 manifest、源码来源、构建产物、测试脚本。
- [ ] 至少一个真实国内游戏包能完成启动、标题菜单、进入正文、存档、读档流程。
- [ ] 控制台能清楚区分：
  - [ ] 插件不存在
  - [ ] 插件下载失败
  - [ ] side module 加载失败
  - [ ] 导出符号缺失
  - [ ] Web 等价实现降级
- [ ] 文档明确告诉用户：Web 版兼容的是插件行为，不是运行 Windows DLL。

## 推荐实施顺序

- [ ] 第 1 步：生成目标游戏插件清单。
- [ ] 第 2 步：实现 `plugins/manifest.json` 读取和预加载。
- [ ] 第 3 步：确认 `/plugin/*.so` 写入 Emscripten FS 后 `Plugins.link` 可以命中。
- [ ] 第 4 步：把 `KAGParser.dll` 作为第一个 side module 样板跑通。
- [ ] 第 5 步：把 `menu.dll` 做成 Web 等价实现，不直接搬 Win32 菜单。
- [ ] 第 6 步：补 `json/fstat/extrans/wutcwf` 等 P0 插件。
- [ ] 第 7 步：接入真实游戏回归测试。
- [ ] 第 8 步：扩展 P1/P2 插件。
- [ ] 第 9 步：补发布缓存、版本校验、错误诊断。
- [ ] 第 10 步：维护不支持清单和游戏级兼容配置。

