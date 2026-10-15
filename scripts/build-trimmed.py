#!/usr/bin/env python3
"""Build a trimmed decode-only FFmpeg prefix from source.

Usage:
    python3 scripts/build-trimmed.py --prefix <dir> [--source-tarball URL | --source-dir DIR]
    python3 scripts/build-trimmed.py --print-flags

Configure flags and the pinned version are parsed from
cmake/FFmpegTrimmed.cmake (single source of truth -- the module's list
items contain no spaces, so token extraction is exact). Produces a shared-
only install tree (<prefix>/bin, include, lib) suitable for
CAUDIO_TRIMMED_ROOT. Host-arch only: for another arch, run on that arch
(CI runners) -- FFmpeg cross-compiles need per-OS toolchains, out of scope.

--pack tars the prefix for deps-private upload. The asset name encodes the
host OS/arch (ffmpeg-trimmed-<ver>-<os>-<arch>.tar.gz); name it honestly --
a misnamed macos-x86_64 asset once shipped arm64 binaries.

Windows runs need a MINGW environment (MSYSTEM=UCRT64 set -- easiest via
ucrt64.exe); bare MSYS without MSYSTEM makes FFmpeg configure refuse.
"""

import argparse
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def cmake_module() -> Path:
    return repo_root() / "cmake" / "FFmpegTrimmed.cmake"


def parse_trim_cmake(text: str) -> tuple[str, list[str]]:
    """Extract pinned version + flag list from cmake/FFmpegTrimmed.cmake."""
    m = re.search(r'set\(CAUDIO_FFMPEG_TRIM_VERSION\s+"([^"]+)"', text)
    if not m:
        print("[ERROR] CAUDIO_FFMPEG_TRIM_VERSION not found in FFmpegTrimmed.cmake",
              file=sys.stderr)
        sys.exit(1)
    version = m.group(1)
    lines = text.splitlines()
    try:
        start = next(i for i, l in enumerate(lines)
                     if l.strip() == "set(FFMPEG_TRIM_FLAGS")
    except StopIteration:
        print("[ERROR] FFMPEG_TRIM_FLAGS block not found in FFmpegTrimmed.cmake",
              file=sys.stderr)
        sys.exit(1)
    flags: list[str] = []
    for line in lines[start + 1:]:
        stripped = line.strip()
        if stripped.startswith("CACHE"):
            break
        code = line.split("#", 1)[0]
        flags.extend(re.findall(r"--[^\s\"']+", code))
    if not flags:
        print("[ERROR] no flags extracted from FFMPEG_TRIM_FLAGS", file=sys.stderr)
        sys.exit(1)
    return version, flags


def asset_name(version: str) -> str:
    os_name = {"Linux": "linux", "Darwin": "macos", "Windows": "windows"}.get(
        platform.system(), platform.system().lower())
    machine = platform.machine().lower()
    arch = {"x86_64": "x86_64", "amd64": "x86_64",
            "aarch64": "arm64", "arm64": "arm64"}.get(machine, machine)
    return f"ffmpeg-trimmed-{version}-{os_name}-{arch}.tar.gz"


def require(prog: str, hint: str) -> str:
    path = shutil.which(prog)
    if not path:
        print(f"[ERROR] '{prog}' not on PATH -- {hint}", file=sys.stderr)
        sys.exit(1)
    return path


def run(cmd: list[str], cwd: Path, dry_run: bool) -> None:
    print(f"+ {' '.join(cmd)}  (in {cwd})")
    if dry_run:
        return
    r = subprocess.run(cmd, cwd=cwd)
    if r.returncode != 0:
        print(f"[ERROR] failed ({r.returncode}): {cmd[0]}", file=sys.stderr)
        sys.exit(1)


def fetch_tarball(url: str, dest: Path, dry_run: bool) -> None:
    print(f"+ download {url} -> {dest}")
    if dry_run:
        return
    try:
        urllib.request.urlretrieve(url, dest)
    except Exception as e:
        print(f"[ERROR] download failed: {e}", file=sys.stderr)
        sys.exit(1)
    print(f"  {dest.stat().st_size} bytes")


def main() -> None:
    ap = argparse.ArgumentParser(description="Build trimmed decode-only FFmpeg.")
    ap.add_argument("--prefix", type=Path,
                    help="install prefix (becomes CAUDIO_TRIMMED_ROOT)")
    ap.add_argument("--source-tarball", default=None,
                    help="FFmpeg source tarball URL (default: ffmpeg.org release for pinned version)")
    ap.add_argument("--source-dir", type=Path, default=None,
                    help="use local FFmpeg source dir instead of downloading")
    ap.add_argument("--ffmpeg-version", default=None,
                    help="override pinned version from FFmpegTrimmed.cmake")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4,
                    help="make -j level (default: cpu count)")
    ap.add_argument("--extra-configure-args", default="",
                    help="space-separated extra args appended after the whitelist "
                         "(e.g. cross-compile --arch/--target-os/--cc)")
    ap.add_argument("--pack", nargs="?", const="auto", default=None, metavar="OUT.tar.gz",
                    help="tar the prefix for upload (default name encodes host os/arch)")
    ap.add_argument("--print-flags", action="store_true",
                    help="print the extracted configure flags, build nothing")
    ap.add_argument("--dry-run", action="store_true",
                    help="print commands without running them")
    args = ap.parse_args()

    text = cmake_module().read_text(encoding="utf-8")
    pinned_version, flags = parse_trim_cmake(text)
    version = args.ffmpeg_version or pinned_version

    if args.print_flags:
        print("\n".join(flags))
        return
    if not args.prefix:
        print("[ERROR] --prefix is required (or use --print-flags)", file=sys.stderr)
        sys.exit(2)

    if not args.dry_run:
        # Dry runs print commands without touching the machine, so tool
        # presence is checked only for real builds.
        if platform.system() == "Windows" and not shutil.which("bash"):
            print("[ERROR] Windows source builds need bash+make on PATH (MSYS2); "
                  "MSVC cannot compile FFmpeg upstream -- consume a prebuilt tree instead.",
                  file=sys.stderr)
            sys.exit(1)
        cc = os.environ.get("CC") or "cc"
        if not shutil.which(cc):
            print(f"[ERROR] C compiler '{cc}' not on PATH (set CC to override)",
                  file=sys.stderr)
            sys.exit(1)
        require("make", "install build essentials (Linux) / Xcode CLT (macOS) / MSYS2 (Windows)")

    prefix = args.prefix.resolve()
    with tempfile.TemporaryDirectory(prefix="trimmed-ffmpeg-") as tmp:
        tmpdir = Path(tmp)
        if args.source_dir:
            src = args.source_dir.resolve()
            if not (src / "configure").exists():
                print(f"[ERROR] no configure in --source-dir {src}", file=sys.stderr)
                sys.exit(1)
        else:
            url = args.source_tarball or f"https://ffmpeg.org/releases/ffmpeg-{version}.tar.xz"
            archive = tmpdir / url.rsplit("/", 1)[-1]
            fetch_tarball(url, archive, args.dry_run)
            print(f"+ extract {archive.name}")
            if not args.dry_run:
                try:
                    with tarfile.open(archive) as tf:
                        tf.extractall(tmpdir)
                except Exception as e:
                    print(f"[ERROR] extract failed: {e}", file=sys.stderr)
                    sys.exit(1)
                tops = [p for p in tmpdir.iterdir() if p.is_dir()]
                if len(tops) != 1 or not (tops[0] / "configure").exists():
                    print("[ERROR] unexpected tarball layout", file=sys.stderr)
                    sys.exit(1)
                src = tops[0]
            else:
                src = tmpdir / f"ffmpeg-{version}"
        build = tmpdir / "build"
        if not args.dry_run:
            build.mkdir()
        # POSIX forms: on Windows these cross into bash (which would eat
        # backslashes), while Path-as-cwd stays native for subprocess.
        configure = [(src / "configure").as_posix(), f"--prefix={prefix.as_posix()}", *flags]
        if args.extra_configure_args:
            configure.extend(args.extra_configure_args.split())
        if platform.system() == "Windows":
            configure = ["bash", *configure]
        run(configure, build, args.dry_run)
        run(["make", f"-j{args.jobs}"], build, args.dry_run)
        run(["make", "install"], build, args.dry_run)
        if platform.system() == "Windows" and not args.dry_run:
            # DLLs belong in bin/ next to the import libs in lib/ (gyan/choco
            # layout); the CMake bundling resolves them via <libdir>/../bin.
            bindir = prefix / "bin"
            bindir.mkdir(exist_ok=True)
            for dll in (prefix / "lib").glob("*.dll"):
                shutil.copy2(dll, bindir / dll.name)

    print(f"trimmed FFmpeg {version} installed to {prefix}")
    print(f"configure with: -DCAUDIO_TRIMMED_ROOT={prefix}")
    if args.pack:
        out = args.pack if args.pack != "auto" else str(prefix.parent / asset_name(version))
        print(f"+ pack {prefix} -> {out}")
        if not args.dry_run:
            # Contents at the tar root (same layout as `tar -C prefix .`),
            # so extracting with -C <root> yields <root>/lib etc. directly.
            with tarfile.open(out, "w:gz") as tf:
                for child in sorted(prefix.iterdir()):
                    tf.add(child, arcname=child.name)
        print(f"upload {out} as a deps-private release asset")


if __name__ == "__main__":
    main()
