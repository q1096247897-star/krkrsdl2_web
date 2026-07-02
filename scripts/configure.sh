#!/usr/bin/env sh
# 交互式生成 .env —— 配置端口和数据目录路径（即"修改目录的脚本"）
# 在群晖部署目录（deploy/）里运行: sh configure.sh
set -eu

ENV_FILE="./.env"

echo "=== krkr-web 部署配置 ==="
echo "（直接回车保留方括号内的默认值）"
echo

# 读取已有值作为默认
cur_port="8080"
cur_games="/volume1/game/18+"
cur_covers="/volume2/base/18x/krkrsdl2_web/covers"
cur_manifest="/volume2/base/18x/krkrsdl2_web/manifest.json"
if [ -f "$ENV_FILE" ]; then
  eval "$(grep -E '^(PORT|GAMES_DIR|COVERS_DIR|MANIFEST_FILE)=' "$ENV_FILE" 2>/dev/null || true)"
  cur_port="${PORT:-8080}"
  cur_games="${GAMES_DIR:-/volume1/game/18+}"
  cur_covers="${COVERS_DIR:-/volume2/base/18x/krkrsdl2_web/covers}"
  cur_manifest="${MANIFEST_FILE:-/volume2/base/18x/krkrsdl2_web/manifest.json}"
fi

read -r -p "对外端口 [$cur_port]: " v; PORT="${v:-$cur_port}"
read -r -p "games 目录宿主路径 [$cur_games]: " v; GAMES_DIR="${v:-$cur_games}"
read -r -p "covers 目录宿主路径 [$cur_covers]: " v; COVERS_DIR="${v:-$cur_covers}"
read -r -p "manifest.json 宿主路径 [$cur_manifest]: " v; MANIFEST_FILE="${v:-$cur_manifest}"

cat > "$ENV_FILE" <<EOF
PORT=$PORT
GAMES_DIR=$GAMES_DIR
COVERS_DIR=$COVERS_DIR
MANIFEST_FILE=$MANIFEST_FILE
EOF

echo
echo "已写入 $ENV_FILE"
echo "提示: 群晖上建议用绝对路径，当前默认扫描 /volume1/game/18+，基础数据 /volume2/base/18x/krkrsdl2_web"
echo "      修改后重启容器生效: docker compose up -d"
