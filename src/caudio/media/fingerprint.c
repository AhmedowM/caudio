#include <caudio/media/fingerprint.h>

#include <caudio/runtime/error.h>

#include <blake3.h>

#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------- */
/* Fingerprint                                                */
/*                                                            */
/* BLAKE3-256 content fingerprint, matching kanariv exactly:   */
/*   BLAKE3(head[0..64K) || tail[size-64K..size) || LE64(size) || LE32(version=1)) */
/*                                                            */
/* Sampled mode (head + tail) is used by default; full mode hashes */
/* the entire file sequentially. Both produce a 32-byte digest. */
/* ---------------------------------------------------------- */

#define BLAKE3_SAMPLE 65536

ca_status ca_media_fingerprint_compute(const char* path,
                                       uint8_t out[32],
                                       ca_error* err) {
    if (err) ca_error_init(err);
    if (!path || path[0] == '\0') {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "path is empty");
        return CA_INVALID_ARGUMENT;
    }
    if (!out) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "out is NULL");
        return CA_INVALID_ARGUMENT;
    }

    FILE* f = fopen(path, "rb");
    if (!f) {
        if (err) ca_error_set(err, CA_IO_ERROR, "cannot open file");
        return CA_IO_ERROR;
    }

    blake3_hasher hasher;
    blake3_hasher_init(&hasher);

    /* Read head (up to 64 KiB) */
    uint8_t buf[BLAKE3_SAMPLE];
    size_t n = fread(buf, 1, sizeof(buf), f);
    blake3_hasher_update(&hasher, buf, n);

    /* Get file size */
    long sz = 0;
    if (fseek(f, 0, SEEK_END) == 0) {
        sz = ftell(f);
        fseek(f, 0, SEEK_SET);
    }

    /* Read tail (up to 64 KiB), if file is larger than head */
    if (sz > (long)sizeof(buf)) {
        long tail_start = sz - (long)sizeof(buf);
        if (fseek(f, tail_start, SEEK_SET) == 0) {
            n = fread(buf, 1, sizeof(buf), f);
            blake3_hasher_update(&hasher, buf, n);
        }
    }

    /* Mix in size (LE64) and version (LE32, version=1) */
    uint64_t sz64 = (uint64_t)sz;
    blake3_hasher_update(&hasher, (const uint8_t*)&sz64, sizeof(sz64));
    uint32_t ver = 1;
    blake3_hasher_update(&hasher, (const uint8_t*)&ver, sizeof(ver));

    blake3_hasher_finalize(&hasher, out, 32);
    fclose(f);

    return CA_OK;
}
