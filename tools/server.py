#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Kirikiri SDL2 Web 游戏库服务端。

静态托管构建产物（index.html / play.html / krkrsdl2.* / games/ / covers/），
并提供游戏库主页所需的 API：

  GET  /api/games      实时列 games/*.xp3        -> [{file, size}]
  GET  /api/manifest   读 manifest.json          -> {games:[...]}
  POST /api/manifest   写 manifest.json          <- {games:[...]}
  POST /api/cover      上传封面到 covers/<name>  <- octet-stream + X-Filename

仅用标准库。无鉴权，仅适合本地/内网，勿裸暴露公网。

用法: python tools/server.py <deploy_dir> [port]
"""

import json
import os
import re
import sys
import threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

XP3_GLOB = "*.xp3"
COVER_RE = re.compile(r"^[A-Za-z0-9._-]+\.(png|jpe?g|webp)$", re.IGNORECASE)
MANIFEST_NAME = "manifest.json"

_write_lock = threading.Lock()


def list_games(root: Path):
    """扫描 root/games/ 下的 *.xp3，返回相对部署根的路径。"""
    games_dir = root / "games"
    if not games_dir.is_dir():
        return []
    out = []
    for p in sorted(games_dir.rglob(XP3_GLOB)):
        if not p.is_file():
            continue
        rel = p.relative_to(root).as_posix()  # 形如 "games/sub/a.xp3"
        out.append({"file": rel, "size": p.stat().st_size})
    return out


def atomic_write_json(path: Path, obj):
    tmp = path.with_suffix(path.suffix + ".tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(obj, f, ensure_ascii=False, indent=2)
    os.replace(tmp, path)


class Handler(SimpleHTTPRequestHandler):
    # SimpleHTTPRequestHandler.__init__ 在构造期间就会处理第一个请求，
    # 所以 self.root 必须在调用 super().__init__ 之前就绪。
    root: Path = None

    def __init__(self, *args, root: Path, **kwargs):
        self.root = root
        super().__init__(*args, directory=str(root), **kwargs)

    # ---- 静态文件修正：默认页回 index.html ----
    def do_GET(self):
        if self.path.startswith("/api/"):
            return self.handle_api_get()
        if self.path in ("", "/"):
            self.path = "/index.html"
        return super().do_GET()

    def do_POST(self):
        if self.path == "/api/manifest":
            return self.api_save_manifest()
        if self.path == "/api/cover":
            return self.api_upload_cover()
        self.send_error(404, "Not Found")

    # ---- API ----
    def handle_api_get(self):
        if self.path == "/api/games":
            return self.send_json({"games": list_games(self.root)})
        if self.path == "/api/manifest":
            p = self.root / MANIFEST_NAME
            if not p.exists():
                return self.send_json({"games": []})
            try:
                with open(p, "r", encoding="utf-8") as f:
                    return self.send_json(json.load(f))
            except (OSError, json.JSONDecodeError) as e:
                return self.send_json({"games": [], "error": str(e)}, 500)
        self.send_error(404, "Not Found")

    def api_save_manifest(self):
        length = int(self.headers.get("Content-Length", 0))
        if length <= 0 or length > 8 * 1024 * 1024:
            return self.send_json({"error": "bad length"}, 400)
        raw = self.rfile.read(length)
        try:
            obj = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as e:
            return self.send_json({"error": "invalid json: " + str(e)}, 400)
        if not isinstance(obj, dict) or not isinstance(obj.get("games"), list):
            return self.send_json({"error": "expected {games:[...]}"}, 400)
        with _write_lock:
            atomic_write_json(self.root / MANIFEST_NAME, obj)
        return self.send_json({"ok": True})

    def api_upload_cover(self):
        name = self.headers.get("X-Filename", "")
        if not COVER_RE.match(name):
            return self.send_json({"error": "invalid filename"}, 400)
        covers = self.root / "covers"
        covers.mkdir(exist_ok=True)
        length = int(self.headers.get("Content-Length", 0))
        if length <= 0 or length > 32 * 1024 * 1024:
            return self.send_json({"error": "bad size"}, 400)
        data = self.rfile.read(length)
        dest = covers / name
        with _write_lock:
            with open(dest, "wb") as f:
                f.write(data)
        return self.send_json({"cover": "covers/" + name})

    # ---- 辅助 ----
    def send_json(self, obj, code=200):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    root = Path(sys.argv[1]).resolve()
    if not root.is_dir():
        print("deploy dir not found: " + str(root), file=sys.stderr)
        sys.exit(1)
    port = int(sys.argv[2]) if len(sys.argv) >= 3 else 8080
    (root / "games").mkdir(exist_ok=True)
    (root / "covers").mkdir(exist_ok=True)
    server = ThreadingHTTPServer(("0.0.0.0", port), lambda *a, **k: Handler(*a, root=root, **k))
    print("serving " + str(root) + " at http://localhost:" + str(port))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nshutting down")


if __name__ == "__main__":
    main()
