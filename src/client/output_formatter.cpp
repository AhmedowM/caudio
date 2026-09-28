#include <caudio/client/output_formatter.hpp>
#include <caudio/db/db_types.hpp>
#include <caudio/engine.hpp>
#include <caudio/ipc/protocol.hpp>
#include <caudio/utils.hpp>
#include <caudio/utils/print.hpp>
#include <chrono>
#include <ctime>
#include <format>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace caudio::client {

std::string OutputFormatter::formatTime(double secs) {
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

std::string OutputFormatter::truncateField(const std::string& s, std::size_t maxLen) {
    if (s.empty())
        return "---";
    if (s.size() <= maxLen)
        return s;
    if (maxLen <= 3)
        return s.substr(0, maxLen);
    return s.substr(0, maxLen - 3) + "...";
}

void OutputFormatter::print(const caudio::ipc::Result& r, std::ostream& os) const {
    if (json_) {
        // Pretty-printed for single-shot human --json; watch streaming uses compact separately.
        caudio::println(os, "{}", caudio::ipc::toJson(r).dump(2));
        return;
    }

    std::visit(
        [&os, this](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, caudio::ipc::Status>) {
                std::string stateStr = caudio::ipc::detail::playbackStateToString(v.state);
                std::string posStr = formatTime(v.pos);
                std::string durStr = formatTime(v.dur);
                int volPct = static_cast<int>(v.vol * 100.0f);
                caudio::println(os, "State: {}", stateStr);
                caudio::println(os, "Pos: {} / {}", posStr, durStr);
                caudio::println(os, "Vol: {}% (muted: {})", volPct, v.muted ? "yes" : "no");
                caudio::println(os, "Shuffle: {} Repeat: {}", v.shuffle ? "on" : "off",
                                caudio::ipc::detail::repeatModeToString(v.repeat));
                if (!v.title.empty() || !v.artist.empty() || v.track_id != 0) {
                    caudio::println(os, "Track: {} - {} [id: {}]", truncateField(v.artist, 40),
                                    truncateField(v.title, 40), v.track_id);
                }
                if (!v.path.empty()) {
                    caudio::println(os, "Path: {}", truncateField(v.path, 80));
                }
                caudio::println(os, "Queue: {}/{}", v.q_idx, v.q_size);
                if (!v.version.empty()) {
                    caudio::println(os, "Version: {}", v.version);
                }
            } else if constexpr (std::is_same_v<T, caudio::ipc::QueueTracks>) {
                std::span<const caudio::db::Track> tracksSpan(v.tracks.data(), v.tracks.size());
                caudio::println(os, "Queue ({} tracks):", tracksSpan.size());
                caudio::println(os, "{:>3} {:>6}  {:<40} {:<40} {:>8}", "#", "ID", "Artist",
                                "Title", "Dur");
                for (std::size_t i = 0; i < tracksSpan.size(); ++i) {
                    const auto& t = tracksSpan[i];
                    caudio::println(os, "{:3} {:6}  {:<40} {:<40} {:>8}", i, t.id,
                                    truncateField(t.artist, 40), truncateField(t.title, 40),
                                    formatTime(t.duration));
                }
            } else if constexpr (std::is_same_v<T, caudio::ipc::VolumeInfo>) {
                int pct = static_cast<int>(v.vol * 100.0f);
                caudio::println(os, "Volume: {}% (muted: {})", pct, v.muted ? "yes" : "no");
            } else if constexpr (std::is_same_v<T, caudio::ipc::LibraryStatsData>) {
                caudio::println(os, "Tracks: {} Queues: {} Playlists: {}", v.tracks, v.queues,
                                v.playlists);
            } else if constexpr (std::is_same_v<T, caudio::ipc::LibraryStatsDetailedData>) {
                caudio::println(os, "Library Stats (Detailed):");
                caudio::println(os, "  Tracks:      {}", v.tracks);
                caudio::println(os, "  Queues:      {}", v.queues);
                caudio::println(os, "  Playlists:   {}", v.playlists);
                double totalDurSec = v.total_duration_ms / 1000.0;
                double totalPlaySec = v.total_play_time_ms / 1000.0;
                caudio::println(os, "  Total Duration:  {}", formatTime(totalDurSec));
                caudio::println(os, "  Total Play Time: {}", formatTime(totalPlaySec));
                if (!v.most_played.empty()) {
                    caudio::println(os, "\n  Most Played Tracks:");
                    caudio::println(os, "  {:>3} {:>6}  {:<40} {:<40} {:>8} {:>10}", "#", "ID",
                                    "Artist", "Title", "Dur", "Plays");
                    for (std::size_t i = 0; i < v.most_played.size(); ++i) {
                        const auto& t = v.most_played[i];
                        caudio::println(os, "  {:3} {:6}  {:<40} {:<40} {:>8} {:>10}", i + 1, t.id,
                                        truncateField(t.artist, 40), truncateField(t.title, 40),
                                        formatTime(t.duration), t.play_count);
                    }
                }
            } else if constexpr (std::is_same_v<T, caudio::ipc::Tracks>) {
                std::span<const caudio::db::Track> tracksSpan(v.tracks.data(), v.tracks.size());
                caudio::println(os, "Tracks ({}):", tracksSpan.size());
                caudio::println(os, "{:>3} {:>6}  {:<40} {:<40} {:>8}", "#", "ID", "Artist",
                                "Title", "Dur");
                for (std::size_t i = 0; i < tracksSpan.size(); ++i) {
                    const auto& t = tracksSpan[i];
                    caudio::println(os, "{:3} {:6}  {:<40} {:<40} {:>8}", i, t.id,
                                    truncateField(t.artist, 40), truncateField(t.title, 40),
                                    formatTime(t.duration));
                }
            } else if constexpr (std::is_same_v<T, caudio::ipc::Playlists>) {
                std::span<const caudio::db::Playlist> playlistSpan(v.playlists.data(),
                                                                   v.playlists.size());
                caudio::println(os, "Playlists ({}):", playlistSpan.size());
                caudio::println(os, "{:>3} {:>6}  {:<40}", "#", "ID", "Name");
                for (std::size_t i = 0; i < playlistSpan.size(); ++i) {
                    const auto& p = playlistSpan[i];
                    caudio::println(os, "{:3} {:6}  {:<40}", i, p.id, truncateField(p.name, 40));
                }
            } else if constexpr (std::is_same_v<T, caudio::ipc::ConfigValue>) {
                caudio::println(os, "{} = {}", v.key, v.value);
            } else if constexpr (std::is_same_v<T, caudio::ipc::ConfigValues>) {
                caudio::println(os, "Config ({} entries):", v.values.size());
                for (const auto& cv :
                     std::span<const caudio::ipc::ConfigValue>(v.values.data(), v.values.size())) {
                    caudio::println(os, "{} = {}", cv.key, cv.value);
                }
            } else if constexpr (std::is_same_v<T, caudio::ipc::PlaylistData>) {
                std::span<const caudio::db::Track> tracksSpan(v.tracks.data(), v.tracks.size());
                caudio::println(os, "Playlist ({} tracks, format: {}):", tracksSpan.size(),
                                v.format);
                caudio::println(os, "{:>3} {:>6}  {:<40} {:<40} {:>8}", "#", "ID", "Artist",
                                "Title", "Dur");
                for (std::size_t i = 0; i < tracksSpan.size(); ++i) {
                    const auto& t = tracksSpan[i];
                    caudio::println(os, "{:3} {:6}  {:<40} {:<40} {:>8}", i, t.id,
                                    truncateField(t.artist, 40), truncateField(t.title, 40),
                                    formatTime(t.duration));
                }
            } else if constexpr (std::is_same_v<T, caudio::ipc::SingleTrack>) {
                const auto& t = v.track;
                caudio::println(os, "Track [{}]", t.id);
                caudio::println(os, "  Path:         {}", t.path);
                caudio::println(os, "  Title:        {}", t.title.empty() ? "(empty)" : t.title);
                caudio::println(os, "  Artist:       {}", t.artist.empty() ? "(empty)" : t.artist);
                caudio::println(os, "  Album:        {}", t.album.empty() ? "(empty)" : t.album);
                caudio::println(os, "  Album Artist: {}",
                                t.album_artist.empty() ? "(empty)" : t.album_artist);
                caudio::println(os, "  Genre:        {}", t.genre.empty() ? "(empty)" : t.genre);
                caudio::println(os, "  Year:         {}", t.year);
                caudio::println(os, "  Track:        {}", t.track_num);
                caudio::println(os, "  Disc:         {}", t.disc_num);
                caudio::println(os, "  Duration:     {}", formatTime(t.duration));
                caudio::println(os, "  Sample Rate:  {}", t.sample_rate);
                caudio::println(os, "  Channels:     {}", t.channels);
                caudio::println(os, "  Bitrate:      {}", t.bitrate);
            } else if constexpr (std::is_same_v<T, caudio::ipc::TrackInfo>) {
                const auto& t = v.track;
                caudio::println(os, "Track [{}]", t.id);
                caudio::println(os, "  Path:         {}", t.path);
                caudio::println(os, "  Title:        {}", t.title.empty() ? "(empty)" : t.title);
                caudio::println(os, "  Artist:       {}", t.artist.empty() ? "(empty)" : t.artist);
                caudio::println(os, "  Album:        {}", t.album.empty() ? "(empty)" : t.album);
                caudio::println(os, "  Album Artist: {}",
                                t.album_artist.empty() ? "(empty)" : t.album_artist);
                caudio::println(os, "  Genre:        {}", t.genre.empty() ? "(empty)" : t.genre);
                caudio::println(os, "  Year:         {}", t.year);
                caudio::println(os, "  Track:        {}", t.track_num);
                caudio::println(os, "  Disc:         {}", t.disc_num);
                caudio::println(os, "  Duration:     {}", formatTime(t.duration));
                caudio::println(os, "  Sample Rate:  {}", t.sample_rate);
                caudio::println(os, "  Channels:     {}", t.channels);
                caudio::println(os, "  Bitrate:      {}", t.bitrate);
                caudio::println(os, "  Play Count:   {}", v.play_count);
                if (v.last_played > 0) {
                    std::time_t tp = static_cast<std::time_t>(v.last_played / 1000);
                    std::tm tm{};
#ifdef _WIN32
                    localtime_s(&tm, &tp);
#else
                    localtime_r(&tp, &tm);
#endif
                    char timeBuf[32];
                    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M", &tm);
                    caudio::println(os, "  Last Played:  {}", timeBuf);
                } else {
                    caudio::println(os, "  Last Played:  (never)");
                }
            } else if constexpr (std::is_same_v<T, caudio::ipc::History>) {
                std::span<const caudio::ipc::HistoryEntry> entriesSpan(v.entries.data(),
                                                                       v.entries.size());
                caudio::println(os, "History ({} entries):", entriesSpan.size());
                caudio::println(os, "{:>3}  {:<20}  {:<40} {:<40} {:>10} {:>8}", "#", "Date",
                                "Artist", "Title", "Pos", "Dur");
                for (std::size_t i = 0; i < entriesSpan.size(); ++i) {
                    const auto& e = entriesSpan[i];
                    // Convert started_at (milliseconds since epoch) to human readable
                    std::time_t t = static_cast<std::time_t>(e.started_at / 1000);
                    std::tm tm{};
#ifdef _WIN32
                    localtime_s(&tm, &t);
#else
                    localtime_r(&t, &tm);
#endif
                    char timeBuf[32];
                    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M", &tm);
                    std::string posStr = formatTime(e.position_ms / 1000.0);
                    std::string durStr = formatTime(e.duration);
                    caudio::println(os, "{:3}  {:<20}  {:<40} {:<40} {:>10} {:>8}", i, timeBuf,
                                    truncateField(e.artist, 40), truncateField(e.title, 40), posStr,
                                    durStr);
                }
            } else if constexpr (std::is_same_v<T, caudio::ipc::Devices>) {
                std::span<const caudio::ipc::DeviceInfo> devicesSpan(v.devices.data(),
                                                                     v.devices.size());
                caudio::println(os, "Devices ({}):", devicesSpan.size());
                caudio::println(os, "{:>3}  {:<40}  {:<60}  {}", "#", "ID", "Name", "Default");
                for (std::size_t i = 0; i < devicesSpan.size(); ++i) {
                    const auto& d = devicesSpan[i];
                    caudio::println(os, "{:3}  {:<40}  {:<60}  {}", i, truncateField(d.id, 40),
                                    truncateField(d.name, 60), d.isDefault ? "*" : "");
                }
            } else if constexpr (std::is_same_v<T, std::monostate>) {
                caudio::println(os, "OK");
            } else if constexpr (std::is_same_v<T, caudio::utils::Error>) {
                // Error variant - print to given stream (caller may pass cerr)
                caudio::println(os, "Error: {} {}", caudio::utils::toString(v.code), v.message);
            } else {
                caudio::println(os, "Unknown result");
            }
        },
        r);
}

int OutputFormatter::printWithStatus(const caudio::ipc::Result& r, std::ostream& out,
                                     std::ostream& err) const {
    bool isError = std::holds_alternative<caudio::utils::Error>(r);
    if (isError) {
        print(r, err);
        return 1;
    }
    print(r, out);
    return 0;
}

} // namespace caudio::client
