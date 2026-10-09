/**
 * @file detail.cpp
 * @brief App-internal reporting helpers (NOT public API).
 * @details Phase 1: builders return text instead of printing.
 */

#include <app/detail.hpp>
#include <caudio/app/format.hpp>
#include <caudio/client/output_formatter.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/utils/error.hpp>
#include <cstdint>
#include <format>
#include <set>
#include <sstream>
#include <string>
#include <variant>

namespace caudio::app::detail {

void emitLine(std::string& blob, const std::string& line) {
    if (!blob.empty())
        blob += '\n';
    blob += line;
}

void appendBlock(std::string& blob, const std::string& text) {
    std::size_t start = 0;
    bool first = blob.empty();
    while (start <= text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(start, end - start);
        bool last = (end == text.size());
        if (last && line.empty())
            break;
        if (!first)
            blob += '\n';
        blob += line;
        first = false;
        start = end + 1;
    }
}

void renderInto(std::string& blob, const caudio::ipc::Result& res) {
    caudio::client::OutputFormatter fmt{false};
    std::ostringstream os;
    fmt.print(res, os);
    appendBlock(blob, os.str());
}

int printAddedText(const caudio::ipc::Result& res, std::set<int64_t>& seen, std::string& out,
                   std::string& err) {
    auto* qt = std::get_if<caudio::ipc::QueueTracks>(&res);
    if (!qt) {
        renderInto(out, res);
        return 0;
    }
    int added = 0;
    for (auto& t : qt->tracks) {
        std::string label = caudio::app::addedLabel(t);
        if (seen.contains(t.id)) {
            emitLine(err, std::format("already in queue: {}", label));
        } else {
            emitLine(out, std::format("Added {}", label));
            seen.insert(t.id);
            ++added;
        }
    }
    return added;
}

void countLineText(int added, std::string& out) {
    if (added == 1)
        emitLine(out, "1 track added");
    else
        emitLine(out, std::format("{} tracks added", added));
}

void renderErrorInto(std::string& err, const caudio::utils::Error& e) {
    caudio::ipc::Result errRes{e};
    caudio::client::OutputFormatter fmt{false};
    std::ostringstream os;
    fmt.print(errRes, os);
    appendBlock(err, os.str());
    if (e.code == caudio::utils::StatusCode::State && e.message == "daemon not running")
        emitLine(err, "hint: run `caudio start` to start the daemon");
}

} // namespace caudio::app::detail
