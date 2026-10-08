#!/usr/bin/env python3
"""Format every C++ source file in the repo with the repo .clang-format.

Usage:
    python3 scripts/format_all.py [--check] [--clang-format BIN] [paths...]

Default scope is the whole tree: *.hpp, *.cpp, *.cppm, excluding build/
and vendor/ directories. Pass explicit paths to limit the run. With
--check nothing is modified; dirty files are listed and exit code is 1.
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

EXTENSIONS = {".hpp", ".cpp", ".cppm"}
EXCLUDE_DIRS = {"build", "vendor", ".git"}


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def collect(paths: list[str]) -> list[Path]:
    roots = [Path(p) for p in paths] if paths else [repo_root()]
    files: list[Path] = []
    for root in roots:
        if not root.exists():
            print(f"[ERROR] path not found: {root}", file=sys.stderr)
            sys.exit(2)
        if root.is_file():
            files.append(root)
            continue
        for p in sorted(root.rglob("*")):
            if not p.is_file() or p.suffix not in EXTENSIONS:
                continue
            rel = p.relative_to(repo_root()) if p.is_relative_to(repo_root()) else p
            if any(part in EXCLUDE_DIRS for part in rel.parts):
                continue
            files.append(p)
    return files


def find_clang_format(explicit: str | None) -> str:
    if explicit:
        return explicit
    found = shutil.which("clang-format")
    if not found:
        print("[ERROR] clang-format not on PATH (pass --clang-format BIN)", file=sys.stderr)
        sys.exit(2)
    return found


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true", help="list files needing formatting, change nothing")
    ap.add_argument("--clang-format", default=None, help="clang-format binary (default: PATH lookup)")
    ap.add_argument("paths", nargs="*", help="files or directories to format (default: whole tree)")
    args = ap.parse_args()

    binary = find_clang_format(args.clang_format)
    files = collect(args.paths)
    if not files:
        print("[ERROR] no matching files", file=sys.stderr)
        return 2

    dirty: list[Path] = []
    failed: list[Path] = []
    for f in files:
        if args.check:
            r = subprocess.run([binary, "--style=file", "--dry-run", "--Werror", str(f)],
                               capture_output=True)
            if r.returncode != 0:
                dirty.append(f)
        else:
            before = f.read_bytes()
            r = subprocess.run([binary, "--style=file", "-i", str(f)], capture_output=True)
            if r.returncode != 0:
                failed.append(f)
                print(f"[ERROR] clang-format failed on {f}\n{r.stderr.decode()}", file=sys.stderr)
            elif f.read_bytes() != before:
                dirty.append(f)

    if args.check and dirty:
        print(f"{len(dirty)} file(s) need formatting:")
        for f in dirty:
            print(f"  {f}")
        return 1
    if failed:
        print(f"{len(failed)} file(s) failed", file=sys.stderr)
        return 2
    if args.check:
        print(f"clean: {len(files)} file(s) checked")
    else:
        print(f"done: {len(dirty)} of {len(files)} file(s) reformatted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
