#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# 扫描游戏目录的 DLL 插件用法，核对 plugins/manifest.json。
# 对应 plugin-compatibility-execution-steps.md §2。
#
# 用法: bash scripts/scan-game-plugins.sh <game_dir> [manifest.json]
#
# 输出三部分：
#   1. 脚本中的插件调用（Plugins.link / unlink / isExistentPlugin），区分注释
#   2. 游戏包内 .dll 文件清单
#   3. 与 manifest 的核对报告（python 处理，避免 bash 大小写匹配的脆弱性）
set -euo pipefail

if [ $# -lt 1 ]; then
	echo "用法: $0 <game_dir> [manifest.json]" >&2
	exit 1
fi

GAME_DIR="$1"
GAME_DIR_ABS="$(cd "$GAME_DIR" && pwd)"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MANIFEST="${2:-$ROOT/plugins/manifest.json}"

if [ ! -d "$GAME_DIR" ]; then
	echo "错误: 游戏目录不存在: $GAME_DIR" >&2
	exit 1
fi

echo "============================================================"
echo " 扫描游戏目录: $GAME_DIR"
echo " 清单文件:     $MANIFEST"
echo "============================================================"

TMP_CALLS="$(mktemp)"
TMP_DLLS="$(mktemp)"
trap 'rm -f "$TMP_CALLS" "$TMP_DLLS"' EXIT

# ---- 2.2 扫描脚本中的插件加载 ----
echo ""
echo "## 2.2 脚本中的插件调用（.tjs/.ks/.tjsi/.js）"
echo ""

grep -rnE '(Plugins\.link|Plugins\.unlink|Storages\.isExistentPlugin)[[:space:]]*\([[:space:]]*"[^"]*\.dll"' \
	"$GAME_DIR" \
	--include="*.tjs" --include="*.ks" --include="*.tjsi" --include="*.js" \
	2>/dev/null > "$TMP_CALLS" || true

if [ ! -s "$TMP_CALLS" ]; then
	echo "[i] 未发现任何插件调用。"
else
	printf "%-28s %-40s %-12s %s\n" "调用方式" "插件名" "是否注释" "位置"
	printf "%-28s %-40s %-12s %s\n" "----------------------------" "----------------------------------------" "------------" "----------------"
	while IFS= read -r line; do
		loc="${line%%:*}"
		rest="${line#*:}"
		lineno="${rest%%:*}"
		content="${rest#*:}"
		trimmed="$(echo "$content" | sed -E 's/^[[:space:]]*//')"
		is_comment="否"
		if [[ "$trimmed" == "//"* ]]; then
			is_comment="是(//)"
		elif [[ "$trimmed" == ";"* ]]; then
			is_comment="是(;)"
		fi
		call_kind="$(echo "$content" | grep -oE '(Plugins\.link|Plugins\.unlink|Storages\.isExistentPlugin)' | head -1)"
		dll_name="$(echo "$content" | sed -E 's/.*"([^"]*\.dll)".*/\1/' | head -1)"
		short_loc="${loc#$GAME_DIR_ABS/}"
		short_loc="${short_loc#$GAME_DIR/}"
		printf "%-28s %-40s %-12s %s:%s\n" "$call_kind" "$dll_name" "$is_comment" "$short_loc" "$lineno"
	done < "$TMP_CALLS"
fi

# ---- 2.3 扫描游戏包内 DLL 文件 ----
echo ""
echo "## 2.3 游戏包内 .dll 文件清单"
echo ""
find "$GAME_DIR" -iname "*.dll" -type f 2>/dev/null > "$TMP_DLLS" || true
if [ ! -s "$TMP_DLLS" ]; then
	echo "[i] 未发现 .dll 文件。"
else
	printf "%-50s %-12s %s\n" "路径" "大小" "文件名"
	printf "%-50s %-12s %s\n" "--------------------------------------------------" "------------" "----------------"
	while IFS= read -r f; do
		size=$(stat -c %s "$f" 2>/dev/null || echo "?")
		short="${f#$GAME_DIR_ABS/}"
		short="${short#$GAME_DIR/}"
		base="$(basename "$f")"
		printf "%-50s %-12s %s\n" "$short" "$size" "$base"
	done < "$TMP_DLLS"
fi

# ---- 2.4 与 manifest 核对（python 处理） ----
echo ""
echo "## 2.4 manifest 核对报告"
echo ""

if [ ! -f "$MANIFEST" ]; then
	echo "[!] manifest 不存在: $MANIFEST，跳过核对。"
	exit 0
fi

GAME_DIR_FOR_PY="$GAME_DIR_ABS" PYTHONIOENCODING=utf-8 python - "$MANIFEST" "$TMP_CALLS" "$TMP_DLLS" <<'PY'
import json, sys, os, re
try:
	sys.stdout.reconfigure(encoding='utf-8')
except Exception:
	pass

manifest_path, calls_path, dlls_path = sys.argv[1:4]
game_dir = os.environ['GAME_DIR_FOR_PY']
manifest = json.load(open(manifest_path, encoding='utf-8'))

# 解析 manifest
plugins = manifest.get('plugins', [])
deferred_orig = manifest.get('deferredPlugins', [])
deferred_lower = set(d.lower() for d in deferred_orig)

# lowercase name -> canonical dllName（主名）
name_to_canonical = {}
canonical_lower = set()      # 主名小写集合
aliases_lower = {}           # lowercase alias -> canonical
for p in plugins:
    canon = p['dllName']
    canonical_lower.add(canon.lower())
    name_to_canonical[canon.lower()] = canon
    for a in p.get('aliases', []):
        name_to_canonical[a.lower()] = canon
        aliases_lower[a.lower()] = canon

# 解析扫描调用
calls = []  # (kind, dll_orig, is_comment, relpath, lineno)
with open(calls_path, encoding='utf-8') as f:
    for raw in f:
        raw = raw.rstrip('\n')
        if not raw:
            continue
        m = re.match(r'^(.*?):(\d+):(.*)$', raw)
        if not m:
            continue
        path, lineno, content = m.group(1), m.group(2), m.group(3)
        km = re.search(r'(Plugins\.link|Plugins\.unlink|Storages\.isExistentPlugin)', content)
        dm = re.search(r'"([^"]*\.dll)"', content)
        if not km or not dm:
            continue
        trimmed = content.lstrip()
        is_comment = trimmed.startswith('//') or trimmed.startswith(';')
        try:
            rel = os.path.relpath(path, game_dir).replace('\\', '/')
        except ValueError:
            rel = path
        calls.append((km.group(1), dm.group(1), is_comment, rel, lineno))

# 解析 dll 文件
dll_files = []  # (relpath, size, basename)
with open(dlls_path, encoding='utf-8') as f:
    for raw in f:
        p = raw.rstrip('\n')
        if not p:
            continue
        try:
            size = os.path.getsize(p)
        except OSError:
            size = '?'
        try:
            rel = os.path.relpath(p, game_dir).replace('\\', '/')
        except ValueError:
            rel = p
        dll_files.append((rel, size, os.path.basename(p)))

# 扫描到的所有 dll 名（小写，含调用和文件）+ 保留原始名
scanned_orig = {}  # lowercase -> 原始名集合
def add_orig(name):
    key = name.lower()
    scanned_orig.setdefault(key, set()).add(name)

called_orig_noncomment = {}  # 非注释调用的小写 -> 原始名集合
for kind, dll, is_comment, rel, lineno in calls:
    add_orig(dll)
    if not is_comment:
        called_orig_noncomment.setdefault(dll.lower(), set()).add(dll)
for rel, size, base in dll_files:
    add_orig(base)

scanned_lower = set(scanned_orig.keys())
called_lower_nc = set(called_orig_noncomment.keys())

def fmt_orig(names):
    return '/'.join(sorted(names))

print("### A. 扫描到但 manifest 未登记（需补充登记或确认是否误用）")
print()
found = False
for d in sorted(scanned_lower):
    if d not in name_to_canonical and d not in deferred_lower:
        print("  [!] " + fmt_orig(scanned_orig[d]))
        found = True
if not found:
    print("  [ok] 无（所有扫描到的插件均已登记）")

print()
print("### B. 扫描到但 manifest 标为 deferred（游戏在用但计划延期，需重排优先级）")
print()
found = False
for d in sorted(scanned_lower):
    if d in deferred_lower:
        print("  [!] " + fmt_orig(scanned_orig[d]))
        found = True
if not found:
    print("  [ok] 无")

print()
print("### C. manifest 已登记但游戏未使用（确认是否其他游戏需要）")
print()
found = False
for c in sorted(canonical_lower):
    if c not in scanned_lower:
        # 找原始主名
        orig = name_to_canonical.get(c, c)
        print("  [i] " + orig)
        found = True
if not found:
    print("  [ok] 无（登记的插件均在游戏中使用）")

print()
print("### D. 大小写变体核对（非注释调用里的大小写变体是否被 aliases 覆盖）")
print()
# 遍历非注释调用的原始名，找与规范主名大小写不同的变体
found = False
seen = set()
for dll_orig in sorted(set(d for _, d, ic, _, _ in calls if not ic)):
    key = dll_orig.lower()
    canon = name_to_canonical.get(key)
    if canon is None:
        continue  # 未登记，A 已报
    if dll_orig == canon:
        continue  # 与主名完全一致，非变体
    if key in seen:
        continue
    seen.add(key)
    # 检查该变体是否登记为别名
    if key in aliases_lower:
        print("  [ok] " + dll_orig + " -> 主名 " + canon + "（已登记别名）")
    else:
        print("  [!] " + dll_orig + " -> 主名 " + canon + "（manifest aliases 未登记此变体）")
    found = True
if not found:
    print("  [i] 无大小写变体调用")

print()
print("### E. 注释中的插件调用（仅供参考，不参与核对）")
print()
found = False
for kind, dll, is_comment, rel, lineno in calls:
    if is_comment:
        print("  [i] " + dll + "  (" + kind + " @ " + rel + ":" + lineno + ")")
        found = True
if not found:
    print("  [i] 无")

PY

echo ""
echo "============================================================"
echo " 扫描完成"
echo "============================================================"
