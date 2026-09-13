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

    static std::string truncateField(const std::string& s, std::size_t maxLen = 40) {
        if (s.size() <= maxLen)
            return s;
        if (maxLen <= 3)
            return s.substr(0, maxLen);
        return s.substr(0, maxLen - 3) + "...";
    }

  public:
    explicit OutputFormatter(bool json = false) : json_(json) {}

    void print(const caudio::cli::Result& r, std::ostream& os) const {
        if (json_) {
            // Pretty-printed for single-shot human --json; watch streaming uses compact separately.
            std::println(os, "{}", caudio::cli::toJson(r).dump(2));
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
                        std::println(os, "Track: {} - {} [id: {}]", truncateField(v.artist, 40),
                                     truncateField(v.title, 40), v.track_id);
                    }
                    if (!v.path.empty()) {
                        std::println(os, "Path: {}", truncateField(v.path, 80));
                    }
                    std::println(os, "Queue: {}/{}", v.q_idx, v.q_size);
                } else if constexpr (std::is_same_v<T, caudio::cli::QueueTracks>) {
                    std::span<const caudio::db::Track> tracksSpan{v.tracks};
                    std::println(os, "Queue ({} tracks):", tracksSpan.size());
                    std::println(os, "{:>3} {:>6}  {:<40} {:<40} {:>8}", "#", "ID", "Artist", "Title",
                                 "Dur");
                    for (std::size_t i = 0; i < tracksSpan.size(); ++i) {
                        const auto& t = tracksSpan[i];
                        std::println(os, "{:3} {:6}  {:<40} {:<40} {:>8}", i, t.id,
                                     truncateField(t.artist, 40), truncateField(t.title, 40),
                                     formatTime(t.duration));
                    }
                } else if constexpr (std::is_same_v<T, caudio::cli::VolumeInfo>) {
                    int pct = static_cast<int>(v.vol * 100.0f);
                    std::println(os, "Volume: {}% (muted: {})", pct, v.muted ? "yes" : "no");
                } else if constexpr (std::is_same_v<T, caudio::cli::LibraryStatsData>) {
                    std::println(os, "Tracks: {} Queues: {} Playlists: {}", v.tracks, v.queues,
                                 v.playlists);
                } else if constexpr (std::is_same_v<T, caudio::cli::Tracks>) {
                    std::span<const caudio::db::Track> tracksSpan{v.tracks};
                    std::println(os, "Tracks ({}):", tracksSpan.size());
                    std::println(os, "{:>3} {:>6}  {:<40} {:<40} {:>8}", "#", "ID", "Artist", "Title",
                                 "Dur");
                    for (std::size_t i = 0; i < tracksSpan.size(); ++i) {
                        const auto& t = tracksSpan[i];
                        std::println(os, "{:3} {:6}  {:<40} {:<40} {:>8}", i, t.id,
                                     truncateField(t.artist, 40), truncateField(t.title, 40),
                                     formatTime(t.duration));
                    }
                } else if constexpr (std::is_same_v<T, caudio::cli::Playlists>) {
                    std::span<const caudio::db::Playlist> playlistSpan{v.playlists};
                    std::println(os, "Playlists ({}):", playlistSpan.size());
                    std::println(os, "{:>3} {:>6}  {:<40}", "#", "ID", "Name");
                    for (std::size_t i = 0; i < playlistSpan.size(); ++i) {
                        const auto& p = playlistSpan[i];
                        std::println(os, "{:3} {:6}  {:<40}", i, p.id, truncateField(p.name, 40));
                    }
                } else if constexpr (std::is_same_v<T, caudio::cli::ConfigValue>) {
                    std::println(os, "{} = {}", v.key, v.value);
                } else if constexpr (std::is_same_v<T, caudio::cli::ConfigValues>) {
                    std::println(os, "Config ({} entries):", v.values.size());
                    for (const auto& cv : std::span<const caudio::cli::ConfigValue>(v.values)) {
                        std::println(os, "{} = {}", cv.key, cv.value);
                    }
                } else if constexpr (std::is_same_v<T, caudio::cli::PlaylistData>) {
                    std::span<const caudio::db::Track> tracksSpan{v.tracks};
                    std::println(os, "Playlist ({} tracks, format: {}):", tracksSpan.size(), v.format);
                    std::println(os, "{:>3} {:>6}  {:<40} {:<40} {:>8}", "#", "ID", "Artist", "Title",
                                 "Dur");
                    for (std::size_t i = 0; i < tracksSpan.size(); ++i) {
                        const auto& t = tracksSpan[i];
                        std::println(os, "{:3} {:6}  {:<40} {:<40} {:>8}", i, t.id,
                                     truncateField(t.artist, 40), truncateField(t.title, 40),
                                     formatTime(t.duration));
                    }
                } else if constexpr (std::is_same_v<T, caudio::cli::SingleTrack>) {
                    const auto& t = v.track;
                    std::println(os, "Track [{}]", t.id);
                    std::println(os, "  Path:         {}", t.path);
                    std::println(os, "  Title:        {}", t.title.empty() ? "(empty)" : t.title);
                    std::println(os, "  Artist:       {}", t.artist.empty() ? "(empty)" : t.artist);
                    std::println(os, "  Album:        {}", t.album.empty() ? "(empty)" : t.album);
                    std::println(os, "  Album Artist: {}", t.album_artist.empty() ? "(empty)" : t.album_artist);
                    std::println(os, "  Genre:        {}", t.genre.empty() ? "(empty)" : t.genre);
                    std::println(os, "  Year:         {}", t.year);
                    std::println(os, "  Track:        {}", t.track_num);
                    std::println(os, "  Disc:         {}", t.disc_num);
                    std::println(os, "  Duration:     {}", formatTime(t.duration));
                    std::println(os, "  Sample Rate:  {}", t.sample_rate);
                    std::println(os, "  Channels:     {}", t.channels);
                    std::println(os, "  Bitrate:      {}", t.bitrate);
                } else if constexpr (std::is_same_v<T, std::monostate>) {
                    std::println(os, "OK");
                } else if constexpr (std::is_same_v<T, caudio::utils::Error>) {
                    // Error variant - print to given stream (caller may pass cerr)
                    std::println(os, "Error: {} {}", caudio::utils::toString(v.code), v.message);
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
