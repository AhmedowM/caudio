# Known issues

Dated log of known-red CI legs and reproduced defects. Entries are written to
be liftable into a GitHub issue body: symptom, evidence, reproduction, current
mitigation, and open work. Prune entries when the underlying bug is fixed and
the fix is verified.

## 2026-10-17 — linux gcc + minsize-lto aborts at startup (SIGABRT)

- **Symptom:** the `caudio` binary aborts before producing any output, even for
  `caudio --version`. Occurs only for the `gcc` compiler with `minsize-lto`
  (`-Os` + IPO/interprocedural optimization), on both `x86_64` and `arm64`.
- **Evidence (bake-off `39025223524`, jobs `117374787253` x86_64 and
  `117374787106` arm64):** `line 11: 6160 Aborted (core dumped) "$BIN" --version`.
  `ldd` resolves every dependency cleanly (`libav*.so` → `$STAGE/bin/../lib/`,
  `libgcc_s.so.1` → system) — so this is not a loader/bundling failure.
  Passing legs on the same machine: gcc `release`, `release-lto`, `minsize`
  (both arches) and every llvm leg.
- **Reproduction:** bake-off leg `linux` / `gcc` / `minsize-lto`.
- **Suspected cause:** GCC 16 miscompilation with `-Os` + IPO + per-linker
  section GC (`--gc-sections`). Not yet root-caused; no output precedes the
  abort, so it is not a config/argument path.
- **Mitigation:** `gcc/minsize-lto` is not a shipped combination. Linux ships
  the llvm leg (clang-21), which passes all four families on both arches.
- **Open work:** reproduce under GCC locally, bisect `-Os` vs IPO vs
  `--gc-sections`, confirm against a newer/older GCC; upstream report if
  isolated to GCC codegen.

## 2026-10-17 — macOS brew-LLVM legs link libc++ by absolute path

- **Symptom:** all `mac`/`brew` bake-off legs fail the portability check; all
  `mac`/`apple` legs pass.
- **Evidence (bake-off `39025223524`, job `117374787140` etc.):** `otool -L` on
  the staged binary shows the bundled FFmpeg correctly as
  `@rpath/libavcodec.62.dylib` … `@rpath/libswresample.6.dylib`, but also
  `/opt/homebrew/opt/llvm/lib/c++/libc++.1.dylib` — an absolute reference into
  the Homebrew LLVM Cellar. The apple leg shows the same `@rpath/libav*` set
  with `/usr/lib/libc++.1.dylib` instead, and passes.
- **Conclusion:** FFmpeg bundling is correct; the failure is the Homebrew LLVM
  driver emitting an absolute install-name for its own `libc++` rather than the
  SDK's `/usr/lib/libc++.1.dylib`. This is a property of the brew toolchain, not
  of the rpath/rewire logic.
- **Mitigation:** brew legs are observatory-only; macOS ships the AppleClang
  leg. A "fix" that rewires the ref to `@rpath/libc++.1.dylib` would ship a
  non-Apple libc++ ABI and additionally depend on a runner-snapshot-versioned
  Cellar file, so it is explicitly *not* done.
- **Open work:** decide whether to drop the brew observatory entirely, or keep
  it as a canary. If kept, document the libc++ exception in the gate.
