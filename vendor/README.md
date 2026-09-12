# Vendor Directory — Provenance Manifest

| File | Upstream URL | Version/Tag | Date | License |
|------|--------------|-------------|------|---------|
| sqlite3.c, sqlite3.h | https://sqlite.org | 3.46.1 | 2024-08-13 | Public Domain |
| blake3.c, blake3.h, blake3_impl.h, blake3_portable.c | https://github.com/BLAKE3-team/BLAKE3 | 1.8.7 | 2023-??-?? | Apache-2.0 / CC0 |
| miniaudio.h | https://github.com/mackron/miniaudio | 0.11.25 | 2026-03-04 | MIT |

## Notes

- **sqlite3**: Amalgamation source from SQLite 3.46.1. Public Domain / Zero-clause BSD.
- **blake3**: BLAKE3 reference implementation v1.8.7. Only `blake3.c`, `blake3.h`, `blake3_impl.h`, and `blake3_portable.c` are vendored. The SIMD dispatch code (`blake3_dispatch.c`) was removed as this build forces portable-only via `BLAKE3_NO_AVX512`, `BLAKE3_NO_AVX2`, `BLAKE3_NO_SSE41`, `BLAKE3_NO_SSE2`, `BLAKE3_USE_NEON=0`. Licensed under Apache-2.0 OR CC0-1.0.
- **miniaudio**: Single-header audio library v0.11.25. Licensed under MIT.