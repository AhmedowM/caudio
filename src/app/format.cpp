/**
 * @file format.cpp
 * @brief Display formatting shared by all frontends.
 * @ingroup caudio_app
 * @details Moved verbatim out of the CLI shell (Phase 0).
 */

#include <caudio/app/format.hpp>
#include <caudio/db/db_types.hpp>
#include <caudio/db/json.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/utils/json.hpp>
#include <cmath>
#include <filesystem>
#include <format>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace caudio::app {

std::string fmtClock(double secs) {
    if (secs < 0)
        secs = 0;
    long total = static_cast<long>(secs);
    if (long h = total / 3600; h > 0)
        return std::format("{:02}:{:02}:{:02}", h, (total % 3600) / 60, total % 60);
    return std::format("{:02}:{:02}", (total % 3600) / 60, total % 60);
}

std::string trackWho(const caudio::ipc::Status& st) {
    if (!st.artist.empty() && !st.title.empty())
        return st.artist + " - " + st.title;
    if (!st.artist.empty())
        return st.artist;
    if (!st.title.empty())
        return st.title;
    if (!st.path.empty()) {
        std::string fn = std::filesystem::path(st.path).filename().generic_string();
        if (!fn.empty())
            return fn;
    }
    return "unknown track";
}

std::string addedLabel(const caudio::db::Track& t) {
    std::string fn = std::filesystem::path(t.path).filename().generic_string();
    std::string who;
    if (!t.artist.empty() && !t.title.empty())
        who = t.artist + " - " + t.title;
    else
        who = t.artist + t.title;
    if (!who.empty() && !fn.empty())
        return who + ": " + fn;
    if (!fn.empty())
        return fn;
    if (!who.empty())
        return who;
    return "track " + std::to_string(t.id);
}

void writePlaylistText(std::ostream& os, const std::vector<caudio::db::Track>& tracks,
                       std::string_view format) {
    if (format == "m3u") {
        os << "#EXTM3U\n";
        for (const auto& t : tracks) {
            int dur = static_cast<int>(std::round(t.duration));
            std::string title = t.title.empty() ? t.path : t.title;
            std::string artist = t.artist.empty() ? "" : t.artist;
            os << "#EXTINF:" << dur;
            if (!artist.empty())
                os << "," << artist << " - " << title;
            else
                os << "," << title;
            os << "\n";
            os << t.path << "\n";
        }
    } else if (format == "pls") {
        os << "[playlist]\n";
        int i = 1;
        for (const auto& t : tracks) {
            os << "File" << i << "=" << t.path << "\n";
            std::string title = t.title.empty() ? t.path : t.title;
            if (!t.artist.empty())
                title = t.artist + " - " + title;
            os << "Title" << i << "=" << title << "\n";
            int dur = static_cast<int>(std::round(t.duration));
            os << "Length" << i << "=" << dur << "\n";
            ++i;
        }
        os << "NumberOfEntries=" << tracks.size() << "\n";
        os << "Version=2\n";
    }
}

void writePlaylistJson(std::ostream& os, const std::vector<caudio::db::Track>& tracks) {
    caudio::utils::Json j;
    j["format"] = "caudio-playlist";
    j["version"] = 1;
    j["tracks"] = caudio::utils::Json::array();
    for (const auto& t : tracks) {
        j["tracks"].push_back(caudio::db::trackToJson(t));
    }
    os << j.dump(2) << "\n";
}

} // namespace caudio::app
