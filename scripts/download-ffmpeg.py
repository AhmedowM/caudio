#!/usr/bin/env python3
"""Download and extract FFmpeg prebuilt for Windows CI.

Downloads the gyan.dev release-full-shared build (~50MB 7z, ~500MB extracted)
and extracts it into ./ffmpeg. With --print-root, prints the FFmpeg install
prefix (the directory containing bin/, include/, and lib/) and nothing else
to stdout; all progress goes to stderr.
"""
import argparse
import os
import shutil
import subprocess
import sys
import urllib.request

FFMPEG_URL = "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-full-shared.7z"


def download(url, output):
    print(f"Downloading FFmpeg from {url} ...", file=sys.stderr)
    urllib.request.urlretrieve(url, output)
    print(f"Downloaded {output}", file=sys.stderr)


def extract(archive, dest):
    print(f"Extracting {archive} to {dest} ...", file=sys.stderr)
    os.makedirs(dest, exist_ok=True)
    seven_zip = shutil.which("7z") or r"C:\Program Files\7-Zip\7z.exe"
    if not os.path.exists(seven_zip):
        print(f"7z.exe not found at {seven_zip} or on PATH", file=sys.stderr)
        sys.exit(1)
    result = subprocess.run(
        [seven_zip, "x", archive, f"-o{dest}", "-y"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"7z extraction failed: {result.stderr}", file=sys.stderr)
        sys.exit(1)
    print(f"Extracted to {dest}", file=sys.stderr)


def ffmpeg_root():
    """Return the FFmpeg install prefix under ./ffmpeg, or None."""
    dest = os.path.join(os.getcwd(), "ffmpeg")
    if not os.path.isdir(dest):
        return None
    for root, _dirs, files in os.walk(dest):
        if "ffmpeg.exe" in files:
            if os.path.basename(root).lower() == "bin":
                return os.path.dirname(root)
            return root
    return None


def main():
    parser = argparse.ArgumentParser(
        description="Download and extract FFmpeg prebuilt for Windows CI"
    )
    parser.add_argument(
        "--print-root",
        action="store_true",
        help="Print FFmpeg install prefix (only output, no progress)",
    )
    args = parser.parse_args()

    archive = os.path.join(os.getcwd(), "ffmpeg-release-full-shared.7z")

    # Always ensure ffmpeg is extracted (progress to stderr only)
    if not ffmpeg_root():
        download(FFMPEG_URL, archive)
        extract(archive, os.path.join(os.getcwd(), "ffmpeg"))
        os.remove(archive)

    if args.print_root:
        root = ffmpeg_root()
        if root:
            print(root)
        else:
            print("FFmpeg not found after extraction", file=sys.stderr)
            sys.exit(1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
