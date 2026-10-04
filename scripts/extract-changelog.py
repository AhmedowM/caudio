import os, sys
from pathlib import Path

raw_tag = os.environ.get("TAG_NAME", "")
if not raw_tag:
    print("[ERROR] TAG_NAME is not set", file=sys.stderr)
    sys.exit(1)

# CHANGELOG headers keep the `v` (`## [v1.0.0-rc3] - DATE`) while tags may or
# may not carry it -- accept both spellings.
num = raw_tag[1:] if raw_tag.startswith("v") else raw_tag
target_prefixes = (f"## [{num}]", f"## [v{num}]")
changelog = Path("CHANGELOG.md")

if not changelog.exists():
    print("[ERROR] CHANGELOG.md not found", file=sys.stderr)
    sys.exit(1)

content = changelog.read_text(encoding="utf-8").splitlines()

notes = []
grabbing = False

for line in content:
    if line.startswith(target_prefixes):
        grabbing = True
        continue  # Skip the header line itself
    if grabbing and line.startswith("## "):
        break
    if grabbing:
        notes.append(line)

result = "\n".join(notes).strip()

if not result:
    print(f"[ERROR] No CHANGELOG section found for version {raw_tag}", file=sys.stderr)
    sys.exit(1)

Path("release-notes.md").write_text(result + "\n", encoding="utf-8")