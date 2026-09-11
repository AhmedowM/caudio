module;
#include <nlohmann/json.hpp>
export module caudio.json;
// Re-export nlohmann types for consumers that use nlohmann::ordered_json directly
export namespace nlohmann {
    using nlohmann::ordered_json;
    using nlohmann::json;
}
export namespace caudio::json {
    using ordered_json = nlohmann::ordered_json;
    using json = nlohmann::json;
}