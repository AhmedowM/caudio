module;
#include <nlohmann/json.hpp>
export module caudio.json;
// Re-export nlohmann types for consumers that use nlohmann::ordered_json directly
export namespace nlohmann {
using nlohmann::json;
using nlohmann::ordered_json;
} // namespace nlohmann
export namespace caudio::json {
using ordered_json = nlohmann::ordered_json;
using json = nlohmann::json;
} // namespace caudio::json