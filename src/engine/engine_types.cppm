module;
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

export module caudio.engine:types;

import caudio.utils;

export namespace caudio::engine {

enum class RepeatMode : int { Off = 0, Queue = 1, One = 2 };

// DEPRECATED: unused — remove in next major. Use QueueState::shuffle
enum class ShuffleMode : int { Off = 0, On = 1 };

enum class PlaybackState : int { Stopped = 0, Ready = 1, Playing = 2, Paused = 3 };

enum class EngineEventType : int {
    None = 0,
    TrackStarted = 1,
    TrackEnded = 2,
    QueueChanged = 3,
    Progress = 4,
    Error = 5
};

struct EngineEvent {
    EngineEventType type{EngineEventType::None};
    int64_t track_id{0};
    int64_t queue_id{1};
    double position{0.0};
    double duration{0.0};
    std::string msg{};
};

struct EngineCallbacks {
    std::function<void(int64_t track_id)> on_track_started{};
    std::function<void(int64_t track_id, double pct)> on_track_ended{};
    std::function<void(int64_t queue_id)> on_queue_changed{};
    std::function<void(caudio::utils::StatusCode err, std::string_view msg)> on_error{};
    void* user{nullptr}; // unused — reserved
};

struct EngineConfig {
    bool enableMonitorThread{true};
    int pollMs{10};
    int gaplessMs{300};
    int historyThresholdPct{60};
    int historyThresholdSecs{90};
    EngineCallbacks callbacks{};
};

struct QueueState {
    bool shuffle{false};
    RepeatMode repeat{RepeatMode::Off};
    std::vector<int64_t> perm{};
    size_t cursor{0};
    int64_t queue_id{1};
};

struct EngineState {
    int shuffleEnabled{0};
    RepeatMode repeatMode{RepeatMode::Off};
    int64_t cursorPos{0};
    int64_t currentTrackId{0};
    float volume{1.0f};
};

} // namespace caudio::engine




