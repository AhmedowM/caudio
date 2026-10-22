#pragma once

/**
 * @file types.hpp
 * @brief Main header for caudio.types -- dependency-free shared value types.
 * @ingroup caudio_types
 * @details Bottom layer of the component graph. Provides the wire enums
 * (RepeatMode, PlaybackState) and the plain-data records (Track, Playlist,
 * QueueItem, HistoryEntry, Bookmark) shared by engine, ipc, db and app, so no
 * component has to include another just to name them. The wire text of the
 * enums (`types/format.hpp`) and its inverse (`types/parse.hpp`) live here
 * too. No dependencies.
 */

#include <caudio/types/format.hpp>
#include <caudio/types/parse.hpp>
#include <caudio/types/record.hpp>
#include <caudio/types/wire.hpp>
