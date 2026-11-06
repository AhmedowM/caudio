#ifndef CAUDIO_H
#define CAUDIO_H

/**
 * @file caudio.h
 * @brief Umbrella header for the caudio domain library.
 *
 * Generic runtime primitives (strings, vectors, byte arrays, results,
 * base64) live in cstdx. caudio depends on cstdx and re-exports the parts
 * that form its public API.
 *
 * Domain headers (types, db, media, player, engine, ipc, service, client,
 * app) are added as the rewrite proceeds.
 */

#include <cstdx/byte_array.h>
#include <cstdx/expected.h>
#include <cstdx/result.h>
#include <cstdx/string.h>
#include <cstdx/vector.h>

#endif /* CAUDIO_H */
