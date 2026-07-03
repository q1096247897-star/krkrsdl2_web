#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""为 krkrsdl2_web（Emscripten 构建）预生成视频转码缓存。

引擎（src/core/visual/sdl2/VideoOvlImpl.cpp）在遇到浏览器无法解码的视频
格式（wmv/asf/avi 等）时，会向
    video_cache/<gamePath>.d/<videoPath去扩展名>.mp4
请求转码后的 mp4。本脚本从 xp3 包中提取这些视频，用 ffmpeg 转码为
H.264/AAC + faststart 的 mp4 并写入上述缓存目录，使弱性能 NAS 只需静态托管
文件即可播放视频（转码只需在性能较强的机器上做一次）。

典型用法（在仓库根目录）：
    python scripts/transcode_videos.py .test-deploy/games/Data.xp3 \
        --out-root .test-deploy

<gamePath> 默认取 xp3 相对 --out-root 的路径（与浏览器 ?data= 一致），例如
.test-deploy/games/Data.xp3 配合 --out-root .test-deploy 得到 games/Data.xp3。
"""

import argparse
import os
import struct
import subprocess
import sys
import tempfile
import zlib

# 必须与引擎 VideoOvlImpl.cpp 中 TVP_EmscIsUnsupportedExt 保持一致。
UNSUPPORTED_EXTS = {
    "wmv", "asf", "avi", "mpg", "mpeg", "mpe",
    "flv", "rm", "rmvb", "vob", "3gp",
}

XP3_MAGIC = bytes([0x58, 0x50, 0x33, 0x0D, 0x0A, 0x20,
                   0x0A, 0x1A, 0x8B, 0x67, 0x01])


def _u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def _u64(b, o):
    return struct.unpack_from("<Q", b, o)[0]


def _i16(b, o):
    return struct.unpack_from("<h", b, o)[0]


def find_xp3_base(f):
    """返回 XP3 magic 在文件中的偏移（无 MZ 头时为 0）。"""
    f.seek(0)
    if f.read(len(XP3_MAGIC)) == XP3_MAGIC:
        return 0
    f.seek(0)
    data = f.read()
    idx = data.find(XP3_MAGIC)
    if idx < 0:
        raise ValueError("未找到 XP3 标记，不是有效的 xp3 文件")
    return idx


def read_index(f, base):
    """读取并返回（必要时解压后的）索引字节。"""
    f.seek(base + len(XP3_MAGIC))
    index_ofs = _u64(f.read(8), 0)
    f.seek(base + index_ofs)
    flag = f.read(1)[0]
    if (flag & 0x07) == 1:  # zlib 压缩索引
        comp_size = _u64(f.read(8), 0)
        f.read(8)  # 解压后大小，丢弃
        return zlib.decompress(f.read(comp_size))
    real_size = _u64(f.read(8), 0)
    return f.read(real_size)


def _iter_chunks(data, start, end):
    """在 [start, end) 内迭代 (tag, payload_start, payload_size)。"""
    pos = start
    while pos + 12 <= end:
        tag = data[pos:pos + 4]
        size = _u64(data, pos + 4)
        payload = pos + 12
        if payload + size > end:
            break
        yield tag, payload, size
        pos = payload + size


def _find_subchunk(data, start, end, tag):
    for name, ps, sz in _iter_chunks(data, start, end):
        if name == tag:
            return ps, sz
    return None, None


def parse_file_entries(index):
    """解析索引，返回 [(name, segments)]。
    segments: [(is_compressed, offset, org_size, arc_size), ...]
    """
    entries = []
    for tag, ps, sz in _iter_chunks(index, 0, len(index)):
        if tag != b"File":
            continue
        file_end = ps + sz
        is_, _ = _find_subchunk(index, ps, file_end, b"info")
        if is_ is None:
            continue
        nlen = _i16(index, is_ + 20)
        fname = index[is_ + 22:is_ + 22 + nlen * 2].decode("utf-16-le")
        ss_, ssz = _find_subchunk(index, ps, file_end, b"segm")
        segs = []
        if ss_ is not None:
            for i in range(ssz // 28):
                pb = ss_ + i * 28
                sflags = _u32(index, pb)
                sstart = _u64(index, pb + 4)
                sorg = _u64(index, pb + 12)
                sarc = _u64(index, pb + 20)
                segs.append(((sflags & 0x07) == 1, sstart, sorg, sarc))
        entries.append((fname, segs))
    return entries


def extract_entry(f, base, segs):
    out = bytearray()
    for is_comp, sstart, _sorg, sarc in segs:
        f.seek(base + sstart)
        raw = f.read(sarc)
        if is_comp:
            raw = zlib.decompress(raw)
        out += raw
    return bytes(out)


def cache_rel_for(game_path, video_name):
    """与引擎 TVP_EmscCacheRelPath 完全一致的缓存相对路径。"""
    slash = video_name.rfind("/")
    dot = video_name.rfind(".")
    if dot != -1 and (slash == -1 or dot > slash):
        stem = video_name[:dot]
    else:
        stem = video_name
    return "video_cache/" + game_path + ".d/" + stem + ".mp4"


def norm_game_path(xp3_path, out_root, override):
    if override:
        return override.replace("\\", "/").strip("/")
    rel = os.path.relpath(os.path.abspath(xp3_path), os.path.abspath(out_root))
    return rel.replace("\\", "/")


def ext_of(name):
    slash = name.rfind("/")
    dot = name.rfind(".")
    if dot != -1 and (slash == -1 or dot > slash):
        return name[dot + 1:].lower()
    return ""


def run_ffmpeg(ffmpeg, src, dst, preset, crf):
    cmd = [
        ffmpeg, "-y", "-i", src,
        "-c:v", "libx264", "-preset", preset, "-crf", str(crf),
        "-pix_fmt", "yuv420p",
        "-c:a", "aac", "-b:a", "128k",
        "-movflags", "+faststart",
        dst,
    ]
    return subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")


def process_xp3(xp3_path, out_root, game_path, ffmpeg, preset, crf,
                force, dry, only_list):
    with open(xp3_path, "rb") as f:
        base = find_xp3_base(f)
        index = read_index(f, base)
        entries = parse_file_entries(index)

    videos = [(n, s) for (n, s) in entries if ext_of(n) in UNSUPPORTED_EXTS]
    print("xp3: %s" % xp3_path)
    print("  gamePath = %s" % game_path)
    print("  需转码视频条目: %d" % len(videos))
    for n, _ in videos:
        print("    - %s" % n)
    if only_list:
        return 0
    if dry:
        for n, _ in videos:
            print("    -> %s" % cache_rel_for(game_path, n))
        return 0

    done = failed = skipped = 0
    with open(xp3_path, "rb") as f:
        for name, segs in videos:
            rel = cache_rel_for(game_path, name)
            dst = os.path.join(out_root, rel.replace("/", os.sep))
            if os.path.exists(dst) and not force:
                print("  跳过(已存在): %s" % rel)
                skipped += 1
                continue
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            data = extract_entry(f, base, segs)
            ext = "." + ext_of(name) if ext_of(name) else ""
            with tempfile.NamedTemporaryFile(suffix=ext, delete=False) as tmp:
                tmp.write(data)
                tmp_path = tmp.name
            try:
                r = run_ffmpeg(ffmpeg, tmp_path, dst, preset, crf)
                if r.returncode == 0:
                    print("  转码完成: %s -> %s (%d 字节)"
                          % (name, rel, os.path.getsize(dst)))
                    done += 1
                else:
                    tail = r.stderr.strip().splitlines()
                    tail = tail[-1] if tail else "(无输出)"
                    print("  转码失败: %s\n    %s" % (name, tail))
                    failed += 1
                    if os.path.exists(dst):
                        os.remove(dst)
            finally:
                try:
                    os.unlink(tmp_path)
                except OSError:
                    pass
    print("  小结: 成功 %d，跳过 %d，失败 %d" % (done, skipped, failed))
    return failed


def main(argv):
    p = argparse.ArgumentParser(
        description="为 krkrsdl2_web 预生成视频转码缓存（xp3 -> mp4）。")
    p.add_argument("xp3", nargs="+", help="xp3 文件路径（可多个）")
    p.add_argument("--out-root", default=".",
                   help="Web 根目录，缓存写到其下 video_cache/。默认当前目录")
    p.add_argument("--game-path", default=None,
                   help="覆盖 gamePath（即 ?data= 的值），默认取 xp3 相对 out-root 的路径")
    p.add_argument("--ffmpeg", default="ffmpeg", help="ffmpeg 路径，默认 ffmpeg")
    p.add_argument("--preset", default="veryfast", help="libx264 预设，默认 veryfast")
    p.add_argument("--crf", type=int, default=23, help="libx264 CRF，默认 23")
    p.add_argument("--force", action="store_true", help="覆盖已存在的缓存")
    p.add_argument("--dry-run", action="store_true", help="列出条目与目标路径，不转码")
    p.add_argument("--list", action="store_true", help="只列出视频条目")
    args = p.parse_args(argv)

    total_failed = 0
    for xp3 in args.xp3:
        if not os.path.isfile(xp3):
            print("文件不存在: %s" % xp3, file=sys.stderr)
            total_failed += 1
            continue
        gp = norm_game_path(xp3, args.out_root, args.game_path)
        total_failed += process_xp3(
            xp3, args.out_root, gp, args.ffmpeg, args.preset,
            args.crf, args.force, args.dry_run, args.list)
    return 1 if total_failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))