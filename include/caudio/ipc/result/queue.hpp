#pragma once

/**
 * @file queue.hpp
 * @brief Queue results for the caudio IPC protocol.
 * @ingroup caudio_ipc
 */

#include <caudio/types/record.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace caudio::ipc {

// ---------------------------------------------------------------------------
// Queue Results
// ---------------------------------------------------------------------------

/**
 * @struct QueueTracks
 * @brief List of tracks in a queue (returned by QueueList, QueueAdd, QueueRemove, etc.).
 * @ingroup caudio_ipc
 *
 * @json_example {"type": "QueueTracks", "tracks": [{"id": 1, "title": "Song", ...}]}
 *
 * @param tracks Vector of tracks in the queue.
 */
struct QueueTracks final {
    /**
     * @brief Vector of tracks in the queue.
     */
    std::vector<caudio::types::Track> tracks{};
};

/**
 * @struct QueueEntry
 * @brief One queue in a Queues listing.
 * @ingroup caudio_ipc
 *
 * @param id Queue id.
 * @param name Queue display name.
 * @param tracks Number of tracks in the queue.
 * @param active Whether this is the active queue.
 */
struct QueueEntry final {
    /**
     * @brief Queue id.
     */
    int64_t id{0};
    /**
     * @brief Queue display name.
     */
    std::string name{};
    /**
     * @brief Number of tracks in the queue.
     */
    std::size_t tracks{0};
    /**
     * @brief Whether this is the active queue.
     */
    bool active{false};
    /**
     * @brief Whether this is an ephemeral direct-play queue.
     */
    bool temp{false};
};

/**
 * @struct Queues
 * @brief List of queues (returned by QueueQueues).
 * @ingroup caudio_ipc
 *
 * @json_example {"type": "Queues", "queues": [{"id": 1, "name": "q", "tracks": 3}]}
 *
 * @param entries Queue entries.
 */
struct Queues final {
    /**
     * @brief Queue entries.
     */
    std::vector<QueueEntry> entries{};
};

/**
 * @struct QueueCreated
 * @brief New queue id (returned by QueueCreate).
 * @ingroup caudio_ipc
 *
 * @json_example {"type": "QueueCreated", "id": 3, "name": "mix"}
 *
 * @param id New queue id.
 * @param name New queue name.
 */
struct QueueCreated final {
    /**
     * @brief New queue id.
     */
    int64_t id{0};
    /**
     * @brief New queue name.
     */
    std::string name{};
};

} // namespace caudio::ipc
