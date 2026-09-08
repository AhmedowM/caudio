module;
#include <chrono>
#include <filesystem>
#include <format>
#include <iostream>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

export module caudio.client:formatter;

import caudio.utils;
import caudio.cli;
import caudio.engine;
import caudio.db;

export namespace caudio::client {

class OutputFormatter {
    bool json_{false};

    static std::string formatTime(double secs) {
        if (secs < 0) secs = 0;
        int total = static_cast<int>(secs);
        int h = total / 3600;
        int m = (total % 3600) / 60;
        int s = total % 60;
        if (h > 0) return std::format("{:02}:{:02}:{:02}", h, m, s);
        return std::format("{:02}:{:02}", m, s);
    }

    static std::string playbackStateToString(caudio::engine::PlaybackState s) {
        using PS = caudio::engine::PlaybackState;
        switch (s) {
        case PS::Stopped: return "Stopped";
        case PS::Ready: return "Ready";
        case PS::Playing: return "Playing";
        case PS::Paused: return "Paused";
        default: return "Unknown";
        }
    }

    static std::string repeatModeToString(caudio::engine::RepeatMode m) {
        using RM = caudio::engine::RepeatMode;
        switch (m) {
        case RM::Off: return "Off";
        case RM::Queue: return "Queue";
        case RM::One: return "One";
        default: return "Unknown";
        }
    }

public:
    explicit OutputFormatter(bool json = false) : json_(json) {}

    void print(const caudio::cli::Result& r, std::ostream& os) const {
        if (json_) {
            // Manual JSON output to avoid duplicate nlohmann symbols in shared libs
            // Use std::format and ordered_json dump via toJsonString would also work,
            // but manual avoids header duplication.
            std::visit(
                [&os](const auto& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, caudio::cli::Status>) {
                        // Use to_underlying for enum values per spec
                        os << std::format(
                            "{{\"type\":\"Status\",\"state\":\"{}\",\"state_value\":{},\"pos\":{},\"dur\":{},"
                            "\"vol\":{},\"muted\":{},\"shuffle\":{},\"repeat\":\"{}\",\"repeat_value\":{},"
                            "\"trackId\":{},\"title\":\"{}\",\"artist\":\"{}\",\"path\":\"{}\",\"qSize\":{},\"qIdx\":{}}}\n",
                            playbackStateToString(v.state), std::to_underlying(v.state), v.pos, v.dur, v.vol,
                            v.muted ? "true" : "false", v.shuffle ? "true" : "false",
                            repeatModeToString(v.repeat), std::to_underlying(v.repeat), v.trackId, v.title,
                            v.artist, v.path, v.qSize, v.qIdx);
                    } else if constexpr (std::is_same_v<T, caudio::cli::QueueTracks>) {
                        os << std::format("{{\"type\":\"QueueTracks\",\"count\":{}}}\n", v.tracks.size());
                    } else if constexpr (std::is_same_v<T, caudio::cli::VolumeInfo>) {
                        os << std::format("{{\"type\":\"VolumeInfo\",\"vol\":{},\"muted\":{}}}\n", v.vol,
                                          v.muted ? "true" : "false");
                    } else if constexpr (std::is_same_v<T, caudio::cli::LibraryStatsData>) {
                        os << std::format("{{\"type\":\"LibraryStats\",\"tracks\":{},\"queues\":{},\"playlists\":{}}}\n",
                                          v.tracks, v.queues, v.playlists);
                    } else if constexpr (std::is_same_v<T, caudio::cli::Tracks>) {
                        os << std::format("{{\"type\":\"Tracks\",\"count\":{}}}\n", v.tracks.size());
                    } else if constexpr (std::is_same_v<T, std::monostate>) {
                        os << "{\"type\":\"Empty\"}\n";
                    } else if constexpr (std::is_same_v<T, caudio::utils::Error>) {
                        os << std::format("{{\"type\":\"Error\",\"code\":\"{}\",\"code_value\":{},\"message\":\"{}\"}}\n",
                                          caudio::utils::toString(v.code), std::to_underlying(v.code), v.message);
                    }
                },
                r);
            return;
        }

        std::visit(
            [&os, this](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, caudio::cli::Status>) {
                    std::string stateStr = playbackStateToString(v.state);
                    std::string posStr = formatTime(v.pos);
                    std::string durStr = formatTime(v.dur);
                    int volPct = static_cast<int>(v.vol * 100.0f);
                    os << std::format("State: {}\n", stateStr);
                    os << std::format("Pos: {} / {}\n", posStr, durStr);
                    os << std::format("Vol: {}% (muted: {})\n", volPct,
                                      v.muted ? "yes" : "no");
                    os << std::format("Shuffle: {} Repeat: {}\n",
                                      v.shuffle ? "on" : "off",
                                      repeatModeToString(v.repeat));
                    if (!v.title.empty() || !v.artist.empty() || v.trackId != 0) {
                        os << std::format("Track: {} - {} [id: {}]\n", v.artist,
                                          v.title, v.trackId);
                    }
                    if (!v.path.empty()) {
                        os << std::format("Path: {}\n", v.path);
                    }
                    os << std::format("Queue: {}/{}  State code: {} Repeat code: {}\n",
                                      v.qIdx, v.qSize,
                                      std::to_underlying(v.state),
                                      std::to_underlying(v.repeat));
                    // Demonstrate filesystem usage
                    if (!v.path.empty()) {
                        std::filesystem::path p{v.path};
                        (void)p.filename();
                    }
                } else if constexpr (std::is_same_v<T, caudio::cli::QueueTracks>) {
                    std::span<const caudio::db::Track> span{v.tracks};
                    os << std::format("QueueTracks ({} tracks):\n", span.size());
                    for (std::size_t i = 0; i < span.size(); ++i) {
                        const auto& t = span[i];
                        os << std::format("{:3} [{}] {} - {} ({})\n", i, t.id,
                                          t.artist, t.title, formatTime(t.duration));
                    }
                } else if constexpr (std::is_same_v<T, caudio::cli::VolumeInfo>) {
                    int pct = static_cast<int>(v.vol * 100.0f);
                    os << std::format("Volume: {}% (muted: {})\n", pct,
                                      v.muted ? "yes" : "no");
                    os << std::format("Volume code: {}\n",
                                      std::to_underlying(caudio::utils::Result::Ok));
                } else if constexpr (std::is_same_v<T, caudio::cli::LibraryStatsData>) {
                    os << std::format("Tracks: {} Queues: {} Playlists: {}\n", v.tracks,
                                      v.queues, v.playlists);
                } else if constexpr (std::is_same_v<T, caudio::cli::Tracks>) {
                    std::span<const caudio::db::Track> span{v.tracks};
                    os << std::format("Tracks ({}):\n", span.size());
                    for (std::size_t i = 0; i < span.size(); ++i) {
                        const auto& t = span[i];
                        os << std::format("{:3} [{}] {} - {} ({})\n", i, t.id,
                                          t.artist, t.title, formatTime(t.duration));
                    }
                } else if constexpr (std::is_same_v<T, std::monostate>) {
                    os << "OK\n";
                } else if constexpr (std::is_same_v<T, caudio::utils::Error>) {
                    // Error variant - print to given stream (caller may pass cerr)
                    os << std::format("Error: {} {}\n",
                                      caudio::utils::toString(v.code), v.message);
                    os << std::format("Code value: {}\n", std::to_underlying(v.code));
                } else {
                    os << "Unknown result\n";
                }
            },
            r);
    }

    // Convenience overload that returns exit code for Error case
    int printWithStatus(const caudio::cli::Result& r, std::ostream& out,
                        std::ostream& err) const {
        bool isError = std::holds_alternative<caudio::utils::Error>(r);
        if (isError) {
            print(r, err);
            return 1;
        }
        print(r, out);
        return 0;
    }
};

} // namespace caudio::client
