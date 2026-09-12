module;
#include <chrono>
#include <format>
#include <iostream>
#include <ostream>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

export module caudio.client:output_formatter;

import caudio.utils;
import caudio.cli;
import caudio.engine;
import caudio.db;

export namespace caudio::client {

class OutputFormatter {
    bool json_{false};

    static std::string formatTime(double secs) {
        if (secs < 0)
            secs = 0;
        int total = static_cast<int>(secs);
        int h = total / 3600;
        int m = (total % 3600) / 60;
        int s = total % 60;
        if (h > 0)
            return std::format("{:02}:{:02}:{:02}", h, m, s);
        return std::format("{:02}:{:02}", m, s);
    }

    // Delegates to protocol single source to avoid duplication.
    static std::string playbackStateToString(caudio::engine::PlaybackState s) {
        return caudio::cli::detail::playbackStateToString(s);
    }

    static std::string repeatModeToString(caudio::engine::RepeatMode m) {
        return caudio::cli::detail::repeatModeToString(m);
    }

  public:
    explicit OutputFormatter(bool json = false) : json_(json) {}

    void print(const caudio::cli::Result& r, std::ostream& os) const {
        if (json_) {
            // Delegate to protocol toJson which properly escapes strings via nlohmann::json.
            std::println(os, "{}", caudio::cli::toJson(r).dump());
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
                    std::println(os, "State: {}", stateStr);
                    std::println(os, "Pos: {} / {}", posStr, durStr);
                    std::println(os, "Vol: {}% (muted: {})", volPct, v.muted ? "yes" : "no");
                    std::println(os, "Shuffle: {} Repeat: {}", v.shuffle ? "on" : "off",
                                 repeatModeToString(v.repeat));
                    if (!v.title.empty() || !v.artist.empty() || v.track_id != 0) {
                        std::println(os, "Track: {} - {} [id: {}]", v.artist, v.title, v.track_id);
                    }
                    if (!v.path.empty()) {
                        std::println(os, "Path: {}", v.path);
                    }
                    std::println(os, "Queue: {}/{}  State code: {} Repeat code: {}", v.q_idx,
                                 v.q_size, std::to_underlying(v.state),
                                 std::to_underlying(v.repeat));
                } else if constexpr (std::is_same_v<T, caudio::cli::QueueTracks>) {
                    std::span<const caudio::db::Track> tracksSpan{v.tracks};
                    std::println(os, "QueueTracks ({} tracks):", tracksSpan.size());
                    for (std::size_t i = 0; i < tracksSpan.size(); ++i) {
                        const auto& t = tracksSpan[i];
                        std::println(os, "{:3} [{}] {} - {} ({})", i, t.id, t.artist, t.title,
                                     formatTime(t.duration));
                    }
                } else if constexpr (std::is_same_v<T, caudio::cli::VolumeInfo>) {
                    int pct = static_cast<int>(v.vol * 100.0f);
                    std::println(os, "Volume: {}% (muted: {})", pct, v.muted ? "yes" : "no");
                    std::println(os, "Volume code: {}",
                                 std::to_underlying(caudio::utils::StatusCode::Ok));
                } else if constexpr (std::is_same_v<T, caudio::cli::LibraryStatsData>) {
                    std::println(os, "Tracks: {} Queues: {} Playlists: {}", v.tracks, v.queues,
                                 v.playlists);
                } else if constexpr (std::is_same_v<T, caudio::cli::Tracks>) {
                    std::span<const caudio::db::Track> tracksSpan{v.tracks};
                    std::println(os, "Tracks ({}):", tracksSpan.size());
                    for (std::size_t i = 0; i < tracksSpan.size(); ++i) {
                        const auto& t = tracksSpan[i];
                        std::println(os, "{:3} [{}] {} - {} ({})", i, t.id, t.artist, t.title,
                                     formatTime(t.duration));
                    }
                } else if constexpr (std::is_same_v<T, caudio::cli::Playlists>) {
                    std::span<const caudio::db::Playlist> playlistSpan{v.playlists};
                    std::println(os, "Playlists ({}):", playlistSpan.size());
                    for (std::size_t i = 0; i < playlistSpan.size(); ++i) {
                        const auto& p = playlistSpan[i];
                        std::println(os, "{:3} [{}] {}", i, p.id, p.name);
                    }
                } else if constexpr (std::is_same_v<T, caudio::cli::ConfigValue>) {
                    std::println(os, "{} = {}", v.key, v.value);
                    std::println(os, "Code value: {}",
                                 std::to_underlying(caudio::utils::StatusCode::Ok));
                } else if constexpr (std::is_same_v<T, caudio::cli::ConfigValues>) {
                    std::println(os, "Config ({} entries):", v.values.size());
                    for (const auto& cv : std::span<const caudio::cli::ConfigValue>(v.values)) {
                        std::println(os, "{} = {}", cv.key, cv.value);
                    }
                } else if constexpr (std::is_same_v<T, std::monostate>) {
                    std::println(os, "OK");
                } else if constexpr (std::is_same_v<T, caudio::utils::Error>) {
                    // Error variant - print to given stream (caller may pass cerr)
                    std::println(os, "Error: {} {}", caudio::utils::toString(v.code), v.message);
                    std::println(os, "Code value: {}", std::to_underlying(v.code));
                } else {
                    std::println(os, "Unknown result");
                }
            },
            r);
    }

    // Convenience overload that returns exit code for Error case
    int printWithStatus(const caudio::cli::Result& r, std::ostream& out, std::ostream& err) const {
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
