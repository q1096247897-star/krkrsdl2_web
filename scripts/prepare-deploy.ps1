# 从 build/ 收集 WASM 产物，连同 docker/ 模板和 server.py
# 组装成自包含的 deploy/ 目录，供上传到群晖。
#
# 用法（在项目根目录）:
#   powershell -ExecutionPolicy Bypass -File scripts\prepare-deploy.ps1
$ErrorActionPreference = "Stop"

$root   = Split-Path -Parent $PSScriptRoot
$build  = Join-Path $root "build"
$docker = Join-Path $root "docker"
$deploy = Join-Path $root "deploy"

if (-not (Test-Path $build)) {
  Write-Error "未找到 build/ 目录。请先构建 WASM:`n  emcmake cmake -B build -DCMAKE_BUILD_TYPE=MinSizeRel`n  cmake --build build"
  exit 1
}

$artifacts = @("krkrsdl2.js","krkrsdl2.wasm","index.html","play.html")
foreach ($n in $artifacts) {
  if (-not (Test-Path (Join-Path $build $n))) {
    Write-Error "缺少构建产物 build/$n。请确认构建成功。"
    exit 1
  }
}

if (Test-Path $deploy) {
  Write-Host "deploy/ 已存在，将更新其中文件（games/covers/manifest.json 保留）..."
}

New-Item -ItemType Directory -Force -Path $deploy | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $deploy "games") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $deploy "covers") | Out-Null

# 构建产物
foreach ($n in $artifacts) {
  Copy-Item (Join-Path $build $n) $deploy -Force
}
# 可选: JS fallback
$wasmJs = Join-Path $build "krkrsdl2.wasm.js"
if (Test-Path $wasmJs) {
  Copy-Item $wasmJs $deploy -Force
  Write-Host "  + krkrsdl2.wasm.js (JS fallback)"
}

# server.py
Copy-Item (Join-Path $root "tools\server.py") $deploy -Force

# docker 模板
Copy-Item (Join-Path $docker "Dockerfile")          $deploy -Force
Copy-Item (Join-Path $docker "docker-compose.yml")  $deploy -Force
Copy-Item (Join-Path $docker ".env.example")        $deploy -Force
Copy-Item (Join-Path $docker ".dockerignore")       $deploy -Force

# configure 脚本（群晖上用）
Copy-Item (Join-Path $root "scripts\configure.sh")  $deploy -Force

# manifest.json 必须是文件（Docker 文件挂载：宿主不存在会变成目录）
$manifest = Join-Path $deploy "manifest.json"
if (Test-Path $manifest) {
  if ((Get-Item $manifest) -is [System.IO.DirectoryInfo]) {
    Write-Error "manifest.json 是目录！请先删除 deploy\manifest.json 目录后重跑。"
    exit 1
  }
} else {
  Set-Content -Path $manifest -Value '{"games":[]}' -Encoding ASCII -NoNewline
}

# 生成 .env（首次）
$envFile = Join-Path $deploy ".env"
if (-not (Test-Path $envFile)) {
  Copy-Item (Join-Path $deploy ".env.example") $envFile -Force
}

Write-Host ""
Write-Host "OK 部署目录已就绪: $deploy"
Write-Host ""
Write-Host "下一步:"
Write-Host "  1. 把整个 deploy/ 上传到群晖 (File Station 传到如 /volume1/krkr-deploy/)"
Write-Host "  2. 群晖 SSH 进该目录，运行 sh configure.sh 修改端口/路径 (可选)"
Write-Host "  3. docker compose up -d --build"
Write-Host "  4. 访问 http://群晖IP:8080/"
