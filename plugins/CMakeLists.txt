# krkrsdl2-web 插件 side module 构建。
#
# 每个 side-module 插件编译为独立的 .so（Emscripten SIDE_MODULE=2），
# 运行时由 plugin-preload.js 写入 Emscripten FS 的 /plugin/<name>.so，
# 引擎 Plugins.link("X.dll") 经 .dll->.so 映射加载它。
#
# 注意：
#   - 必须与主 wasm 使用同一个 emsdk 版本编译。
#   - 第三方源码放在 plugins/third_party/<name>/，逐个补 include 路径。
#   - 需在根 CMakeLists.txt 用 OPTION_ENABLE_EXTERNAL_PLUGINS 之类的开关
#     add_subdirectory(plugins)，避免污染非 Web 构建。

# 插件需要 krkrz 各子目录的头文件。用 CMAKE_SOURCE_DIR 拼绝对路径，
# 避免 add_subdirectory 后相对路径被误解析到 plugins/ 下。
set(KRKR_PLUGIN_INCLUDE_DIRS
	${CMAKE_SOURCE_DIR}/src/core/base/sdl2
	${CMAKE_SOURCE_DIR}/src/core/environ/sdl2
	${CMAKE_SOURCE_DIR}/src/core/msg/sdl2
	${CMAKE_SOURCE_DIR}/src/core/sdl2
	${CMAKE_SOURCE_DIR}/src/core/sound/sdl2
	${CMAKE_SOURCE_DIR}/src/core/utils/sdl2
	${CMAKE_SOURCE_DIR}/src/core/visual/sdl2
	${CMAKE_SOURCE_DIR}/src/plugins
	${CMAKE_SOURCE_DIR}/src/config
	${CMAKE_SOURCE_DIR}/external
	${CMAKE_SOURCE_DIR}/external/krkrz
	${CMAKE_SOURCE_DIR}/external/krkrz/base
	${CMAKE_SOURCE_DIR}/external/krkrz/environ
	${CMAKE_SOURCE_DIR}/external/krkrz/extension
	${CMAKE_SOURCE_DIR}/external/krkrz/msg
	${CMAKE_SOURCE_DIR}/external/krkrz/sound
	${CMAKE_SOURCE_DIR}/external/krkrz/tjs2
	${CMAKE_SOURCE_DIR}/external/krkrz/utils
	${CMAKE_SOURCE_DIR}/external/krkrz/visual
	${CMAKE_SOURCE_DIR}/external/krkrz/visual/gl
	${CMAKE_SOURCE_DIR}/external/krkrz/visual/opengl
	${CMAKE_SOURCE_DIR}/external/krkrz/external
	${CMAKE_SOURCE_DIR}/external/krkrz/external/freetype/include
	${CMAKE_SOURCE_DIR}/external/simde
	${CMAKE_SOURCE_DIR}/external/zlib
)

# side-module 公共编译/链接选项。
# 注意：krkrsdl2 移除了 krkrz 的插件 stub 机制（FuncStubs.h 仅 1 行声明，主引擎
# 不调 TVPInitImportStub）。因此 side-module 目前无法调用 TVPGetScriptDispatch 等
# 主 wasm 函数，只能用于不依赖 TVP 导出函数的插件（如 Minimal）。依赖 stub 的
# 传统 krkrz 插件（如 KAGParser）需先重建 stub 机制，见 plugin-compatibility 文档。
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

	target_include_directories(${target_name} PRIVATE ${KRKR_PLUGIN_INCLUDE_DIRS})
endfunction()

# 第三方源码就绪后再取消注释对应目标。源码未拉取时构建会失败，所以默认全部注释。
# 拉取方式见 plugin-compatibility-execution-steps.md §7.1 / §8.1 / §9。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/KAGParser/Main.cpp")
krkr_plugin_side_module(KAGParser
	third_party/KAGParser/Main.cpp
	third_party/KAGParser/KAGParser.cpp
)
endif()

# 最小 side-module：仅 V2Link/V2Unlink，用于验证 side-module 加载链路。
krkr_plugin_side_module(Minimal
	src/minimal/Minimal.cpp
)

# krkr_plugin_side_module(json
#	third_party/json/JSON.cpp
# )
#
# krkr_plugin_side_module(fstat
#	third_party/fstat/fstat.cpp
# )
#
# krkr_plugin_side_module(wutcwf
#	third_party/SamplePlugin/wutcwf/wutcwf.cpp
# )
#
# krkr_plugin_side_module(extrans
#	third_party/SamplePlugin/extrans/extrans.cpp
# )
#
# krkr_plugin_side_module(csvParser
#	third_party/csvParser/csvParser.cpp
# )
#
# krkr_plugin_side_module(saveStruct
#	third_party/saveStruct/saveStruct.cpp
# )
#
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/layerExImage/Main.cpp")
krkr_plugin_side_module(layerExImage
	third_party/layerExImage/LayerExImage.cpp
	third_party/layerExImage/Main.cpp
	third_party/ncbind/ncbind.cpp
)
target_compile_definitions(layerExImage PRIVATE
	TVP_PLUGIN_NAME=layerExImage
)
target_include_directories(layerExImage PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/third_party/layerExImage
	${CMAKE_CURRENT_SOURCE_DIR}/third_party/ncbind
)
endif()

# 把编译出的 .so 收集到 plugins/web/plugin/，供 plugin-preload.js 取用。
# 仅在存在已构建的 side-module 目标时启用。
get_property(_krkr_plugin_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
set(_krkr_side_modules "")
foreach(_t ${_krkr_plugin_targets})
	get_target_property(_type ${_t} TYPE)
	get_target_property(_suffix ${_t} SUFFIX)
	if(_type STREQUAL "SHARED_LIBRARY" AND _suffix STREQUAL ".so")
		list(APPEND _krkr_side_modules ${_t})
	endif()
endforeach()

if(_krkr_side_modules)
	set(_krkr_plugin_out_dir ${CMAKE_BINARY_DIR}/plugins/web/plugin)
	add_custom_target(krkr_plugin_collect ALL
		COMMAND ${CMAKE_COMMAND} -E make_directory ${_krkr_plugin_out_dir}
		DEPENDS ${_krkr_side_modules}
		COMMENT "Collecting plugin side modules into plugins/web/plugin"
	)
	foreach(_t ${_krkr_side_modules})
		add_custom_command(TARGET krkr_plugin_collect POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				$<TARGET_FILE:${_t}> ${_krkr_plugin_out_dir}/$<TARGET_FILE_NAME:${_t}>
		)
	endforeach()
endif()
