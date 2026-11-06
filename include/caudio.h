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
#include <caudio/runtime/cond.h>
#include <caudio/runtime/error.h>
#include <caudio/runtime/fs.h>
#include <caudio/runtime/mutex.h>
#include <caudio/runtime/path.h>
#include <caudio/runtime/rwlock.h>
#include <caudio/runtime/status.h>
#include <caudio/runtime/thread.h>
#include <caudio/runtime/tls.h>
#include <caudio/runtime/time.h>

#endif /* CAUDIO_H */
