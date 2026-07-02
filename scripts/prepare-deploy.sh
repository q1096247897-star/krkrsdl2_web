#!/usr/bin/env bash
# 从 build/ 收集 WASM 产物，连同 docker/ 模板和 server.py
# 组装成自包含的 deploy/ 目录，供上传到群晖。
# 对应 plugin-compatibility-execution-steps.md §14。
#
# 用法: bash scripts/prepare-deploy.sh [build_dir]
#   build_dir 默认 build，可用环境变量 BUILD_DIR 覆盖（如 build-web）。
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-${1:-$root/build}}"
docker_dir="$root/docker"
deploy="$root/deploy"

if [ ! -d "$build" ]; then
  echo "错误: 未找到构建目录 $build。请先构建 WASM:" >&2
  echo "  export PATH=\"/d/emsdk/upstream/emscripten:\$PATH\"" >&2
  echo "  emcmake cmake -S . -B $build -G Ninja -DOPTION_ENABLE_EXTERNAL_PLUGINS=ON -DCMAKE_BUILD_TYPE=MinSizeRel -DSDL2_DIR=/d/emsdk/upstream/emscripten/cache/sysroot/lib/cmake/SDL2" >&2
  echo "  cmake --build $build" >&2
  exit 1
fi

# ---- §14.1 校验主程序产物 ----
artifacts=(krkrsdl2.js krkrsdl2.wasm index.html play.html plugin-preload.js)
for n in "${artifacts[@]}"; do
  if [ ! -f "$build/$n" ]; then
    echo "错误: 缺少构建产物 $build/$n" >&2
    exit 1
  fi
done

[ -d "$deploy" ] && echo "deploy/ 已存在，将更新其中文件（games/covers/manifest.json 保留）..."
mkdir -p "$deploy/games" "$deploy/covers" "$deploy/plugins/shims" "$deploy/plugin"

# ---- 主程序 + 预加载器 ----
cp "$build"/{krkrsdl2.js,krkrsdl2.wasm,index.html,play.html,plugin-preload.js} "$deploy/"

# ---- 插件清单 + web-shim 脚本 ----
cp "$root/plugins/manifest.json" "$deploy/plugins/manifest.json"
if [ -d "$root/plugins/shims" ]; then
  cp "$root"/plugins/shims/*.js "$deploy/plugins/shims/" 2>/dev/null || true
fi

# ---- side-module 产物（.so）：从 build/plugins 拷到 deploy/plugin ----
if [ -d "$build/plugins" ]; then
  cp "$build"/plugins/*.so "$deploy/plugin/" 2>/dev/null || true
fi

# 可选 JS fallback
if [ -f "$build/krkrsdl2.wasm.js" ]; then
  cp "$build/krkrsdl2.wasm.js" "$deploy/"
  echo "  + krkrsdl2.wasm.js (JS fallback)"
fi

cp "$root/tools/server.py" "$deploy/"
cp "$docker_dir/Dockerfile" "$docker_dir/docker-compose.yml" \
   "$docker_dir/.env.example" "$docker_dir/.dockerignore" "$deploy/"
cp "$root/scripts/configure.sh" "$deploy/"

# manifest.json 必须是文件（Docker 文件挂载：宿主不存在会变成目录）
if [ -e "$deploy/manifest.json" ] && [ ! -f "$deploy/manifest.json" ]; then
  echo "错误: manifest.json 是目录！请先删除 deploy/manifest.json 目录后重跑。" >&2
  exit 1
fi
[ -f "$deploy/manifest.json" ] || printf '%s' '{"games":[]}' > "$deploy/manifest.json"

# 生成 .env（首次）
[ -f "$deploy/.env" ] || cp "$deploy/.env.example" "$deploy/.env"

# ---- §14.3 发布包校验 ----
echo
echo "==== 发布包校验 ===="
errors=0

# 主程序必需文件
for f in krkrsdl2.js krkrsdl2.wasm index.html play.html plugin-preload.js; do
  if [ ! -f "$deploy/$f" ]; then echo "  [缺失] $f"; errors=$((errors+1)); fi
done
# 插件清单
if [ ! -f "$deploy/plugins/manifest.json" ]; then echo "  [缺失] plugins/manifest.json"; errors=$((errors+1)); fi

# 清单内 webModule 路径与实际文件一致性校验
if [ -f "$deploy/plugins/manifest.json" ]; then
  PYTHONIOENCODING=utf-8 python - "$deploy/plugins/manifest.json" <<'PY' || errors=$((errors+1))
import json, sys, os
sys.stdout.reconfigure(encoding='utf-8')
m = json.load(open(sys.argv[1], encoding='utf-8'))
deploy = os.path.dirname(os.path.dirname(os.path.abspath(sys.argv[1])))
missing_shim = []   # web-shim 缺失：错误（主路径，必须可用）
missing_so = []     # side-module 缺失：警告（可能因 stub 阻塞暂未编译）
for p in m.get('plugins', []):
    wm = p.get('webModule', '')
    if not wm: continue
    path = os.path.join(deploy, wm.replace('/', os.sep))
    if os.path.exists(path): continue
    if wm.endswith('.js') or p.get('implementationType') == 'web-shim':
        missing_shim.append(p['dllName'] + ' -> ' + wm)
    else:
        missing_so.append(p['dllName'] + ' -> ' + wm)
if missing_shim:
    print('  [错误] web-shim 文件缺失（主路径，必须存在）：')
    for x in missing_shim: print('    ' + x)
    sys.exit(1)
if missing_so:
    print('  [警告] side-module .so 缺失（可能因 stub 阻塞暂未编译，不影响发布）：')
    for x in missing_so: print('    ' + x)
PY
fi

# schemaVersion 校验
if [ -f "$deploy/plugins/manifest.json" ]; then
  sv=$(PYTHONIOENCODING=utf-8 python - "$deploy/plugins/manifest.json" <<'PY' 2>/dev/null || echo "?"
import json,sys
sys.stdout.reconfigure(encoding='utf-8')
print(json.load(open(sys.argv[1],encoding='utf-8')).get('schemaVersion','?'))
PY
)
  if [ "$sv" != "1" ]; then echo "  [警告] manifest schemaVersion=$sv（预期 1），可能不兼容当前预加载器"; fi
fi

if [ "$errors" -gt 0 ]; then
  echo
  echo "错误: 发布包校验失败，$errors 个问题。" >&2
  exit 1
fi

echo "  [ok] 主程序产物齐全"
echo "  [ok] 插件清单存在且 webModule 路径与发布包一致"
echo "  [ok] side-module .so: $(ls "$deploy/plugin/"*.so 2>/dev/null | wc -l) 个"
echo "  [ok] web-shim .js: $(ls "$deploy/plugins/shims/"*.js 2>/dev/null | wc -l) 个"

echo
echo "OK 部署目录已就绪: $deploy"
echo
echo "下一步:"
echo "  1. 把整个 deploy/ 上传到群晖"
echo "  2. 群晖 SSH 进该目录，运行 sh configure.sh 修改端口/路径 (可选)"
echo "  3. docker compose up -d --build"
echo "  4. 访问 http://群晖IP:8080/"
