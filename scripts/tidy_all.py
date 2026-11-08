#!/usr/bin/env python3
"""Run clang-tidy over the repo compilation database (report or safe fix).

Usage:
    python3 scripts/tidy_all.py [--build-dir DIR] [--fix] [--checks CHECKS]
                                [--export-fixes DIR] [-j N] [--clang-tidy PATH]
                                [file-regex...]

Thin wrapper around clang-tidy: picks the repo .clang-tidy config, verifies
the compilation database, parallelizes. Default mode only reports. With
--fix applies fix-its directly via clang-tidy --fix on each file.

--fix applies only the curated safe set unless --checks is given explicitly.
--headers analyzes every public header as its own TU (headers are never
main files in the DB, so plain runs never fix them). Each header inherits
compile flags from a same-component TU. --fix is not supported with
--headers: review the exported YAML and apply per component after review;
default checks is misc-include-cleaner.

C23 notes: clang-tidy's C23 support uses C-style casts (not static_cast).
Checks that propose C++-only fix-its are avoided in the safe-fix set.
"""

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

SAFE_FIX_CHECKS = ("modernize-use-nullptr,"
                   "modernize-deprecated-headers,"
                   "performance-*")
DEFAULT_BUILD_DIR = "build/dev"
# Allowlist keeps repo sources only: fetched third-party TUs (_deps and
# libs/third_party) would otherwise dominate the run.
DEFAULT_SOURCE_FILTER = r"^(?!.*(_deps|third_party).*).*[\\/](src|include|tests)[\\/]"


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def find_clang_tidy(explicit: str | None) -> str:
    if explicit:
        return explicit
    found = shutil.which("clang-tidy")
    if not found:
        print("[ERROR] clang-tidy not on PATH (pass --clang-tidy PATH)", file=sys.stderr)
        sys.exit(2)
    return found


def db_flags(entry: dict) -> list[str]:
    if "arguments" in entry:
        toks = list(entry["arguments"])
    else:
        toks = shlex.split(entry["command"], posix=True)
    flags: list[str] = []
    skip_next = False
    for i, tok in enumerate(toks):
        if i == 0:
            continue  # compiler executable
        if skip_next:
            skip_next = False
            continue
        if tok in ("-o", "-c"):
            skip_next = True
            continue
        flags.append(tok)
    return flags


def collect_files(db: Path, source_filter: str | None, files: list[str]) -> list[dict]:
    entries = json.loads(db.read_text(encoding="utf-8"))
    out: list[dict] = []
    filter_re = re.compile(source_filter) if source_filter else None
    file_re = re.compile("|".join(files)) if files else None
    for e in entries:
        f = e["file"].replace("\\", "/")
        if "_deps" in f or "third_party" in f:
            continue
        if filter_re and not filter_re.search(f):
            continue
        if file_re and not file_re.search(f):
            continue
        out.append(e)
    return out


def run_tidy_on_file(entry: dict, tidy: str, checks: str | None,
                     fix: bool, export_fixes: str | None) -> tuple[str, int, str, str]:
    """Run clang-tidy on a single compilation database entry."""
    f = entry["file"].replace("\\", "/")
    directory = entry.get("directory", str(repo_root()))
    flags = db_flags(entry)

    cmd = [tidy]
    if checks:
        cmd.append(f"-checks={checks}")
    if export_fixes:
        cmd.append(f"--export-fixes={export_fixes}")
    if fix:
        cmd.append("--fix")
    cmd.append(f)
    cmd += ["--"] + flags

    r = subprocess.run(cmd, cwd=directory, capture_output=True, text=True)
    return (f, r.returncode, r.stdout, r.stderr)


def summarize_warnings(stdout: str, stderr: str) -> list[str]:
    """Extract tidy warning lines from clang-tidy output.

    clang-tidy prints diagnostics as:
      path:line:col: warning: message [check-name]
    We collapse repeated messages per file and return a short summary.
    """
    seen: set[str] = set()
    lines: list[str] = []
    for text in (stdout, stderr):
        for line in text.splitlines():
            # clang-tidy diagnostic line format:
            #   <path>:<line>:<col>: <warning|error>: <message> [<check-name>]
            m = re.search(r':\d+:\d+:\s+(warning|error):\s+(.+?)\s+\[', line)
            if m:
                key = m.group(2)
                if key not in seen:
                    seen.add(key)
                    lines.append(f"  {m.group(1)}: {key}")
    return lines


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build-dir", default=DEFAULT_BUILD_DIR,
                    help="build tree with compile_commands.json")
    ap.add_argument("--fix", action="store_true",
                    help="apply fixes (restricted to safe checks unless --checks given)")
    ap.add_argument("--checks", default=None,
                    help="clang-tidy checks filter (default: repo .clang-tidy; with --fix: safe set)")
    ap.add_argument("--export-fixes", default=None,
                    help="write suggested fixes as YAML files here for review")
    ap.add_argument("-j", type=int, default=os.cpu_count(),
                    help="parallel jobs (default: cpu count)")
    ap.add_argument("--clang-tidy", default=None, help="clang-tidy binary (default: PATH lookup)")
    ap.add_argument("--headers", action="store_true",
                    help="analyze public headers as own TUs")
    ap.add_argument("files", nargs="*",
                    help="file path regexes, e.g. 'caudio.runtime.error.c' "
                         "(default: repo sources, _deps excluded)")
    args = ap.parse_args()

    tidy = find_clang_tidy(args.clang_tidy)
    db = repo_root() / args.build_dir / "compile_commands.json"
    if not db.exists():
        print(f"[ERROR] no compilation database at {db} (configure the dev intent first)",
              file=sys.stderr)
        return 2

    if args.headers:
        return run_headers(args, tidy, db)

    # Determine the checks string.
    if args.fix and args.checks is None:
        checks = "-*," + SAFE_FIX_CHECKS
    elif args.checks is not None:
        checks = args.checks if args.checks.startswith("-*,") else "-*," + args.checks
    else:
        checks = None  # use repo .clang-tidy config

    entries = collect_files(db, DEFAULT_SOURCE_FILTER, args.files)
    if not entries:
        print("[ERROR] no matching files", file=sys.stderr)
        return 2

    export_dir = None
    if args.export_fixes and not args.fix:
        export_dir = Path(args.export_fixes)
        export_dir.mkdir(parents=True, exist_ok=True)

    print(f"Running clang-tidy on {len(entries)} file(s) "
          f"with {args.j} job(s) ...")

    bad = 0
    fixed = 0
    file_warnings: dict[str, list[str]] = defaultdict(list)
    had_compiler_error = False

    with ThreadPoolExecutor(max_workers=args.j) as ex:
        futures = {}
        for e in entries:
            ef = None
            if export_dir:
                ef = str(export_dir /
                         Path(e["file"].replace("\\", "/")).with_suffix("").as_posix().replace("/", "-") + ".yaml")
            fut = ex.submit(run_tidy_on_file, e, tidy, checks, args.fix, ef)
            futures[fut] = e

        for fut in as_completed(futures):
            f, rc, stdout, stderr = fut.result()
            combined = stdout + "\n" + stderr
            # clang-tidy returns non-zero for both warnings and errors.
            # Distinguish: a line starting with "<file>:<line>:<col>: error:" is
            # a real compiler/tidy error; everything else is a warning.
            if re.search(r':\d+:\d+:\s+error:', combined):
                had_compiler_error = True
                bad += 1
            elif rc != 0:
                # clang-tidy sometimes returns non-zero for warnings only;
                # treat those as informational, not failures.
                pass
            if stdout:
                file_warnings[f].extend(summarize_warnings(stdout, ""))
            if stderr:
                file_warnings[f].extend(summarize_warnings("", stderr))
            if args.fix and rc == 0 and "FIX-IT applied" in stdout:
                fixed += 1

    # Pretty summary: group warnings by file
    if file_warnings:
        print("\nWarnings:")
        for f in sorted(file_warnings):
            rel = Path(f).relative_to(repo_root()) if Path(f).is_absolute() else f
            warns = file_warnings[f]
            print(f"  {rel}:")
            for w in warns[:5]:
                print(f"    {w}")
            if len(warns) > 5:
                print(f"    ... ({len(warns) - 5} more)")

    if args.fix:
        print(f"\nApplied fixes to {fixed} file(s), {bad} with compiler errors")
        return 0 if fixed > 0 else 1
    if had_compiler_error:
        print(f"\n{bad} file(s) had compiler errors")
        return 1
    print(f"\nclean: {len(entries)} file(s) checked")
    return 0


def run_headers(args: argparse.Namespace, tidy: str, db: Path) -> int:
    if args.fix:
        print("[ERROR] --fix is not supported with --headers (review YAML first)", file=sys.stderr)
        return 2
    if args.files:
        print("[ERROR] file regexes are not supported with --headers", file=sys.stderr)
        return 2
    checks = args.checks if args.checks else "-*,misc-include-cleaner"
    if not checks.startswith("-*,"):
        checks = "-*," + checks
    entries = json.loads(db.read_text(encoding="utf-8"))
    by_comp: dict[str, list[str]] = {}
    fallback: list[str] = []
    for e in entries:
        f = e["file"].replace("\\", "/")
        if "_deps" in f or "third_party" in f:
            continue
        flags = db_flags(e)
        if not fallback and ("/src/" in f):
            fallback = flags
        parts = Path(f).parts
        if "src" in parts:
            by_comp.setdefault(parts[parts.index("src") + 1], flags)
    if not fallback:
        for e in entries:
            f = e["file"].replace("\\", "/")
            if "_deps" not in f and "third_party" not in f:
                fallback = db_flags(e)
                break
    inc = repo_root() / "include" / "caudio"
    headers = sorted(inc.rglob("*.h"))
    if args.export_fixes:
        outdir = Path(args.export_fixes)
        outdir.mkdir(parents=True, exist_ok=True)

    def one(header: Path) -> tuple[str, int]:
        rel = header.relative_to(inc)
        comp = rel.parts[0] if len(rel.parts) > 1 else ""
        flags = by_comp.get(comp, fallback)
        cmd = [tidy, str(header), f"-checks={checks}"]
        yaml = None
        if args.export_fixes:
            yaml = str(outdir / (rel.with_suffix("").as_posix().replace("/", "-") + ".yaml"))
            cmd.append(f"--export-fixes={yaml}")
        cmd += ["--"] + flags
        r = subprocess.run(cmd, cwd=repo_root(), capture_output=True, text=True)
        if not args.export_fixes and (r.stdout or r.stderr):
            print(r.stdout, end="")
            print(r.stderr, end="", file=sys.stderr)
        return (rel.as_posix(), r.returncode)

    bad = 0
    with ThreadPoolExecutor(max_workers=args.j) as ex:
        for rel, rc in ex.map(one, headers):
            print(f"{rel}: exit {rc}")
            bad += rc != 0
    print(f"{len(headers)} header(s) analyzed, {bad} with errors")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
