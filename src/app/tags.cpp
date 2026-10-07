/**
 * @file tags.cpp
 * @brief Track tag command handlers.
 * @ingroup caudio_app
 * @details Moved verbatim out of the CLI shell (Phase 0): tag editing and
 * tag reads with optional single-field selection.
 */

#include <algorithm>
#include <array>
#include <caudio/app/core.hpp>
#include <caudio/client/output_formatter.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/json.hpp>
#include <caudio/utils/print.hpp>
#include <cstdint>
#include <expected>
#include <iostream>
#include <string>
#include <string_view>
#include <variant>

namespace caudio::app {

int App::tagEdit(std::int64_t id, const std::string& field, const std::string& value,
                 bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::TagEdit{id, field, value}};
    return confirm(sendRaw(cmd), asJson, std::format("Updated {} for track {}", field, id));
}

int App::tagGet(std::int64_t id, const std::string& field, bool asJson) {
    static const std::array<std::string_view, 8> tagFields{
        "title", "artist", "album", "album_artist",
        "genre", "year", "track_number", "disc_number"};
    if (!field.empty() &&
        std::find(tagFields.begin(), tagFields.end(), field) == tagFields.end()) {
        caudio::println(std::cerr, "tag get: unknown field '{}' (expected one of "
                                   "title|artist|album|album_artist|genre|year|"
                                   "track_number|disc_number)",
                        field);
        return 1;
    }
    caudio::ipc::Command cmd{caudio::ipc::TagGet{id}};
    auto res = sendRaw(cmd);
    if (!res)
        return printErr(res.error());
    if (field.empty()) {
        if (asJson)
            return printJson(*res);
        caudio::client::OutputFormatter fmt{false};
        fmt.print(*res, std::cout);
        return 0;
    }
    auto* st = std::get_if<caudio::ipc::SingleTrack>(&*res);
    if (!st) {
        caudio::client::OutputFormatter fmt{asJson};
        fmt.print(*res, std::cout);
        return 0;
    }
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
    if (asJson) {
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
        caudio::println("{}", j.dump());
        return 0;
    }
    caudio::println("{}", value);
    return 0;
}

} // namespace caudio::app
