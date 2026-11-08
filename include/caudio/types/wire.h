#ifndef CAUDIO_TYPES_WIRE_H
#define CAUDIO_TYPES_WIRE_H

/**
 * @file caudio/types/wire.h
 * @brief Dependency-free wire vocabulary shared by the engine, database and
 *        IPC layers.
 *
 * The numeric values and the textual spellings returned by the `_name`
 * functions are part of the wire contract and must not change. Parsing is
 * case-sensitive and accepts exactly the spellings produced by `_name`.
 */

#include <stdint.h>

#include <caudio/api.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Repeat mode for queue advancement.
 *
 * Mirrors `kanariv::types::RepeatMode`.
 */
typedef enum ca_repeat_mode : uint8_t {
    CA_REPEAT_OFF = 0, /**< Advance normally and wrap according to the queue. */
    CA_REPEAT_ALL = 1, /**< Repeat the queue after its last item. */
    CA_REPEAT_ONE = 2  /**< Repeat the current track without advancing. */
} ca_repeat_mode;

/**
 * @brief Playback state machine (wire value).
 *
 * Mirrors `kanariv::types::PlaybackState`.
 */
typedef enum ca_playback_state : uint8_t {
    CA_PLAYBACK_STOPPED = 0, /**< No track is currently playing. */
    CA_PLAYBACK_READY = 1,   /**< Initialized and ready to play. */
    CA_PLAYBACK_PLAYING = 2, /**< Audio playback is active. */
    CA_PLAYBACK_PAUSED = 3   /**< Paused with the position retained. */
} ca_playback_state;

/**
 * @brief Durable identity category for a track's bytes.
 *
 * Mirrors `kanariv::types::SourceKind`.
 */
typedef enum ca_source_kind : uint8_t {
    CA_SOURCE_PATH = 0,        /**< Ordinary filesystem path (legacy default). */
    CA_SOURCE_EXTERNAL = 1,    /**< Stable host-issued identifier. */
    CA_SOURCE_IMPORTED = 2,    /**< App-private copy created by the host. */
    CA_SOURCE_UNAVAILABLE = 3  /**< Known but cannot currently be reopened. */
} ca_source_kind;

/**
 * @brief Engine event types emitted through the internal event queue.
 *
 * Mirrors `kanariv::types::EngineEventType`.
 */
typedef enum ca_engine_event_type : uint8_t {
    CA_ENGINE_EVENT_NONE = 0,           /**< No event. */
    CA_ENGINE_EVENT_TRACK_STARTED = 1,  /**< A new track started. */
    CA_ENGINE_EVENT_TRACK_ENDED = 2,    /**< Reserved; callbacks carry pct. */
    CA_ENGINE_EVENT_QUEUE_CHANGED = 3,  /**< Queue or shuffle switched. */
    CA_ENGINE_EVENT_PROGRESS = 4,       /**< Periodic progress tick (~500 ms). */
    CA_ENGINE_EVENT_ERROR = 5           /**< Internal error forwarded. */
} ca_engine_event_type;

/**
 * @brief Return the wire text of a repeat mode.
 * @return "Off", "All", "One", or "Unknown".
 */
[[nodiscard]] CAUDIO_API const char *ca_repeat_mode_name(ca_repeat_mode mode);

/**
 * @brief Parse the wire text of a repeat mode.
 * @param text Case-sensitive text.
 * @param out  Receives the parsed value on success.
 * @return true on success, false for unknown text or a nullptr argument.
 */
[[nodiscard]] CAUDIO_API bool ca_repeat_mode_parse(const char *text,
                                                   ca_repeat_mode *out);

/**
 * @brief Return the wire text of a playback state.
 * @return "Stopped", "Ready", "Playing", "Paused", or "Unknown".
 */
[[nodiscard]] CAUDIO_API const char *ca_playback_state_name(
    ca_playback_state state);

/**
 * @brief Parse the wire text of a playback state.
 * @param text Case-sensitive text.
 * @param out  Receives the parsed value on success.
 * @return true on success, false for unknown text or a nullptr argument.
 */
[[nodiscard]] CAUDIO_API bool ca_playback_state_parse(const char *text,
                                                      ca_playback_state *out);

/**
 * @brief Return the serialized name of a source kind.
 * @return "path", "external", "imported", or "unavailable".
 */
[[nodiscard]] CAUDIO_API const char *ca_source_kind_name(ca_source_kind kind);

/**
 * @brief Parse a source kind from its serialized name.
 * @param text Case-sensitive name.
 * @param out  Receives the parsed value on success.
 * @return true on success, false for unknown text or a nullptr argument.
 */
[[nodiscard]] CAUDIO_API bool ca_source_kind_parse(const char *text,
                                                   ca_source_kind *out);

/**
 * @brief Return the name of an engine event type.
 * @return "None", "TrackStarted", "TrackEnded", "QueueChanged", "Progress",
 *         "Error", or "Unknown".
 */
[[nodiscard]] CAUDIO_API const char *ca_engine_event_type_name(
    ca_engine_event_type type);

/**
 * @brief Parse an engine event type name.
 * @return true on success, false for unknown text or a nullptr argument.
 */
[[nodiscard]] CAUDIO_API bool ca_engine_event_type_parse(
    const char *text, ca_engine_event_type *out);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_TYPES_WIRE_H */
