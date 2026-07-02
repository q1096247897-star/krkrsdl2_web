#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Kirikiri SDL2 Web 游戏库服务端。

静态托管构建产物(index.html / play.html / krkrsdl2.* / games/ / covers/)，
并提供游戏库主页所需的 API：

  GET  /api/games      实时列 games/*.xp3        -> [{file, size}]
  GET  /api/manifest   读 manifest.json          -> {games:[...]}
  POST /api/manifest   写 manifest.json          <- {games:[...]}
  POST /api/cover      上传封面到 covers/<name>  <- octet-stream + X-Filename

仅用标准库。无鉴权，仅适合本地/内网，请勿暴露公网。

用法: python tools/server.py <deploy_dir> [port]
"""

import json
import os
import re
import sys
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

XP3_GLOB = "*.xp3"
COVER_RE = re.compile(r"^[A-Za-z0-9._-]+\.(png|jpe?g|webp)$", re.IGNORECASE)
MANIFEST_NAME = "manifest.json"
# Support specifying manifest path via environment variable, keep backward compatibility
MANIFEST_PATH = os.environ.get("MANIFEST_PATH", MANIFEST_NAME)

_write_lock = threading.Lock()


def list_games(root: Path):
    """List all *.xp3 files under root/games/, return relative path to deploy root."""
    games_dir = root / "games"
    if not games_dir.is_dir():
        return []
    out = []
    for p in sorted(games_dir.rglob(XP3_GLOB)):
        if not p.is_file():
            continue
        rel = p.relative_to(root).as_posix()  # format: "games/sub/a.xp3"
        out.append({"file": rel, "size": p.stat().st_size})
    return out


def atomic_write_json(path: Path, obj, retries=5, retry_delay=0.2):
    """Atomic write JSON with retry for transient file lock issues (e.g. Synology DSM)."""
    tmp = path.with_suffix(path.suffix + ".tmp")
    for i in range(retries):
        try:
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(obj, f, ensure_ascii=False, indent=2)
                os.fsync(f.fileno())  # Force flush to disk, avoid filesystem cache issues
            os.replace(tmp, path)
            return
        except OSError as e:
            # Retry only on "device or resource busy" errors
            if e.errno == 16 and i < retries - 1:
                time.sleep(retry_delay)
                # Clean up failed temp file
                if tmp.exists():
                    try:
                        tmp.unlink()
                    except:
                        pass
                continue
            # Other errors or retry exhausted, clean up and re-raise
            if tmp.exists():
                try:
                    tmp.unlink()
                except:
                    pass
            raise


class Handler(SimpleHTTPRequestHandler):
    # SimpleHTTPRequestHandler.__init__ processes the first request during initialization,
    # so self.root must be set before calling super().__init__.
    root: Path = None

    def __init__(self, *args, root: Path, **kwargs):
        self.root = root
        super().__init__(*args, directory=str(root), **kwargs)

    # ---- Static file fix: default to index.html ----
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
            p = Path(MANIFEST_PATH) if Path(MANIFEST_PATH).is_absolute() else self.root / MANIFEST_PATH
            if not p.exists():
                return self.send_json({"games": []})
            try:
                with open(p, "r", encoding="utf-8") as f:
                    return self.send_json(json.load(f))
            except (OSError, json.JSONDecodeError) as e:
                return self.send_json({"games": [], "error": "Failed to read manifest: " + str(e)}, 500)
        self.send_error(404, "Not Found")

    def api_save_manifest(self):
        length = int(self.headers.get("Content-Length", 0))
        if length <= 0 or length > 8 * 1024 * 1024:
            return self.send_json({"error": "Invalid request body size"}, 400)
        raw = self.rfile.read(length)
        try:
            obj = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as e:
            return self.send_json({"error": "Invalid JSON: " + str(e)}, 400)
        if not isinstance(obj, dict) or not isinstance(obj.get("games"), list):
            return self.send_json({"error": "Expected {games:[...]} structure"}, 400)
        with _write_lock:
            manifest_path = Path(MANIFEST_PATH) if Path(MANIFEST_PATH).is_absolute() else self.root / MANIFEST_PATH
            atomic_write_json(manifest_path, obj)
        return self.send_json({"ok": True})

    def api_upload_cover(self):
        name = self.headers.get("X-Filename", "")
        if not COVER_RE.match(name):
            return self.send_json({"error": "Invalid filename"}, 400)
        covers = self.root / "covers"
        covers.mkdir(exist_ok=True)
        length = int(self.headers.get("Content-Length", 0))
        if length <= 0 or length > 32 * 1024 * 1024:
            return self.send_json({"error": "Invalid file size"}, 400)
        data = self.rfile.read(length)
        dest = covers / name
        with _write_lock:
            with open(dest, "wb") as f:
                f.write(data)
        return self.send_json({"cover": "covers/" + name})

    # ---- Helper ----
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
        print("Deploy directory does not exist: " + str(root), file=sys.stderr)
        sys.exit(1)
    port = int(sys.argv[2]) if len(sys.argv) >= 3 else 8080
    (root / "games").mkdir(exist_ok=True)
    (root / "covers").mkdir(exist_ok=True)
    server = ThreadingHTTPServer(("0.0.0.0", port), lambda *a, **k: Handler(*a, root=root, **k))
    print("Serving " + str(root) + ", access at http://localhost:" + str(port))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down")


if __name__ == "__main__":
    main()