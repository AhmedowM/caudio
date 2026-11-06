#ifndef CAUDIO_H
#define CAUDIO_H

/**
 * @file caudio.h
 * @brief Umbrella header for the caudio domain library.
 *
 * ca_runtime (filesystem, paths, time, threads, endpoints, sockets, shared
 * memory, status codes, error records) lives here. cstdx is a separate,
 * independently consumable shared library and is not part of the caudio
 * umbrella.
 *
 * Domain headers (types, db, media, player, engine, ipc, service, client,
 * app) are added as the rewrite proceeds.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>
#include <caudio/runtime/status.h>

#endif /* CAUDIO_H */
