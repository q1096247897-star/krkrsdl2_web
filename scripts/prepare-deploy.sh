#!/usr/bin/env bash
# 从 build/ 收集 WASM 产物，连同 docker/ 模板和 server.py
# 组装成自包含的 deploy/ 目录，供上传到群晖。
# 用法: bash scripts/prepare-deploy.sh
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="$root/build"
docker_dir="$root/docker"
deploy="$root/deploy"

if [ ! -d "$build" ]; then
  echo "错误: 未找到 build/ 目录。请先构建 WASM:" >&2
  echo "  emcmake cmake -B build -DCMAKE_BUILD_TYPE=MinSizeRel" >&2
  echo "  cmake --build build" >&2
  exit 1
fi

artifacts=(krkrsdl2.js krkrsdl2.wasm index.html play.html)
for n in "${artifacts[@]}"; do
  if [ ! -f "$build/$n" ]; then
    echo "错误: 缺少构建产物 build/$n" >&2
    exit 1
  fi
done

[ -d "$deploy" ] && echo "deploy/ 已存在，将更新其中文件（games/covers/manifest.json 保留）..."
mkdir -p "$deploy/games" "$deploy/covers"

cp "$build"/{krkrsdl2.js,krkrsdl2.wasm,index.html,play.html,plugin-preload.js} "$deploy/"

# 插件兼容清单与 web 产物（side-module .so / web-shim .js）
mkdir -p "$deploy/plugins"
cp "$root/plugins/manifest.json" "$deploy/plugins/manifest.json"
if [ -d "$root/plugins/web" ]; then
  mkdir -p "$deploy/plugins/web"
  cp -r "$root/plugins/web/." "$deploy/plugins/web/" 2>/dev/null || true
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

echo
echo "OK 部署目录已就绪: $deploy"
echo
echo "下一步:"
echo "  1. 把整个 deploy/ 上传到群晖"
echo "  2. 群晖 SSH 进该目录，运行 sh configure.sh 修改端口/路径 (可选)"
echo "  3. docker compose up -d --build"
echo "  4. 访问 http://群晖IP:8080/"
