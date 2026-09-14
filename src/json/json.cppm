/**
 * @file json.cppm
 * @brief JSON utilities module for caudio.
 * @ingroup caudio_json
 *
 * Provides type aliases for nlohmann::ordered_json and nlohmann::json,
 * re-exported in the caudio::json namespace for convenient access.
 * Consumers can use caudio::json::ordered_json directly.
 */

module;
#include <nlohmann/json.hpp>
export module caudio.json;

// Re-export nlohmann types for consumers that use nlohmann::ordered_json directly
export namespace nlohmann {
using nlohmann::json;
using nlohmann::ordered_json;
} // namespace nlohmann

/**
 * @namespace caudio::json
 * @brief JSON type aliases for caudio.
 * @ingroup caudio_json
 *
 * Provides convenient type aliases for nlohmann JSON types.
 */
export namespace caudio::json {

/**
 * @brief Alias for nlohmann::ordered_json.
 *
 * Maintains key insertion order during serialization. Preferred for
 * IPC protocol messages where field order aids readability and debugging.
 * @ingroup caudio_json
 */
using ordered_json = nlohmann::ordered_json;

/**
 * @brief Alias for nlohmann::json.
 *
 * Standard unordered JSON type. Use when order is not important.
 * @ingroup caudio_json
 */
using json = nlohmann::json;

} // namespace caudio::json