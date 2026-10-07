/**
 * @file format.cpp
 * @brief Display formatting shared by all frontends.
 * @ingroup caudio_app
 * @details Moved verbatim out of the CLI shell (Phase 0).
 */

#include <caudio/app/format.hpp>
#include <caudio/ipc/result.hpp>
#include <filesystem>
#include <format>
#include <string>

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

} // namespace caudio::app
