/**
 * @file tags.cpp
 * @brief Track tag command handlers.
 * @ingroup caudio_app
 * @details Phase 1: returns outcome data; the frontend renders.
 */

#include <algorithm>
#include <caudio/app/core.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/json.hpp>
#include <cstdint>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <variant>

namespace caudio::app {

bool isTagField(std::string_view field) {
    return std::find(kTagFields.begin(), kTagFields.end(), field) != kTagFields.end();
}

AppResult App::tagEdit(std::int64_t id, const std::string& field, const std::string& value) {
    caudio::ipc::Command cmd{caudio::ipc::TagEdit{id, field, value}};
    return confirm(sendRaw(cmd), std::format("Updated {} for track {}", field, id));
}

AppResult App::tagGet(std::int64_t id) {
    caudio::ipc::Command cmd{caudio::ipc::TagGet{id}};
    return confirm(sendRaw(cmd));
}

std::expected<TagValue, caudio::utils::Error> App::tagValue(std::int64_t id,
                                                            const std::string& field) {
    if (!isTagField(field)) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg,
                                                        "unknown tag field '" + field + "'")};
    }
    caudio::ipc::Command cmd{caudio::ipc::TagGet{id}};
    auto res = sendRaw(cmd);
    if (!res)
        return std::unexpected{res.error()};
    auto* st = std::get_if<caudio::ipc::SingleTrack>(&*res);
    if (!st)
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                        "tag get: no track in result")};
    const auto& t = st->track;
    bool numeric = false;
    std::string value;
    if (field == "title")
        value = t.title;
    else if (field == "artist")
        value = t.artist;
    else if (field == "album")
        value = t.album;
    else if (field == "album_artist")
        value = t.album_artist;
    else if (field == "genre")
        value = t.genre;
    else if (field == "year") {
        numeric = true;
        value = std::to_string(t.year);
    } else if (field == "track_number") {
        numeric = true;
        value = std::to_string(t.track_num);
    } else if (field == "disc_number") {
        numeric = true;
        value = std::to_string(t.disc_num);
    }
    caudio::utils::Json j = caudio::utils::Json::object();
    if (numeric) {
        try {
            j[field] = std::stoll(value);
        } catch (...) {
            j[field] = value;
        }
    } else {
        j[field] = value;
    }
    return TagValue{value, j.dump()};
}

} // namespace caudio::app
