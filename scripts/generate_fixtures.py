#!/usr/bin/env python3
"""Regenerate decode-whitelist fixtures from tests/fixtures/sample.wav.

Covers the FFmpeg trim whitelist guard in tests/test_ffmpeg.cpp:
mp3, flac, m4a (aac), opus, wma (wmav2), plus the pre-existing ogg/wav.

Usage:
    py -3 scripts/generate_fixtures.py [--check]

--check verifies every fixture decodes (via ffprobe stream presence)
without rewriting files. Requires ffmpeg/ffprobe on PATH.
"""

import argparse
import subprocess
import sys
from pathlib import Path

FIXTURES = Path(__file__).resolve().parent.parent / "tests" / "fixtures"

# (output name, ffmpeg args) -- 1s source, small bitrates, deterministic-ish.
TARGETS = [
    ("sample.mp3", ["-c:a", "libmp3lame", "-b:a", "64k"]),
    ("sample.flac", ["-c:a", "flac"]),
    ("sample.m4a", ["-c:a", "aac", "-b:a", "64k"]),
    ("sample.opus", ["-c:a", "libopus", "-b:a", "32k"]),
    ("sample.wma", ["-c:a", "wmav2", "-b:a", "64k"]),
]


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(f"failed: {' '.join(cmd)}\n{r.stderr[-2000:]}", file=sys.stderr)
        sys.exit(1)


def decodable(path):
    r = subprocess.run(
        [
            "ffprobe",
            "-v",
            "error",
            "-show_entries",
            "stream=codec_name",
            "-of",
            "csv=p=0",
            str(path),
        ],
        capture_output=True,
        text=True,
    )
    return r.returncode == 0 and bool(r.stdout.strip())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    src = FIXTURES / "sample.wav"
    if not src.exists():
        print(f"source missing: {src}", file=sys.stderr)
        sys.exit(2)

    failed = []
    for name, enc in TARGETS:
        out = FIXTURES / name
        if not args.check:
            run(["ffmpeg", "-y", "-v", "error", "-i", str(src), *enc, str(out)])
        ok = out.exists() and decodable(out)
        print(("ok  " if ok else "FAIL") + f" {name}")
        if not ok:
            failed.append(name)

    # Pre-existing fixtures must also stay decodable.
    for name in ("sample.wav", "sample.ogg"):
        p = FIXTURES / name
        ok = p.exists() and decodable(p)
        print(("ok  " if ok else "FAIL") + f" {name}")
        if not ok:
            failed.append(name)

    if failed:
        print(f"missing/broken: {', '.join(failed)}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
