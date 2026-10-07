/**
 * @file detail.cpp
 * @brief App-internal reporting helpers (NOT public API).
 * @details Moved verbatim out of the CLI shell (Phase 0).
 */

#include <app/detail.hpp>
#include <caudio/app/format.hpp>
#include <caudio/client/output_formatter.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/utils/print.hpp>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <variant>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace caudio::app::detail {

bool useColor() {
    if (std::getenv("NO_COLOR") != nullptr)
        return false;
#ifdef _WIN32
    return ::_isatty(::_fileno(stdout)) != 0;
#else
    return ::isatty(STDOUT_FILENO) != 0;
#endif
}

int printAdded(const caudio::ipc::Result& res, std::set<int64_t>& seen) {
    auto* qt = std::get_if<caudio::ipc::QueueTracks>(&res);
    if (!qt) {
        caudio::client::OutputFormatter fmt{false};
        fmt.print(res, std::cout);
        return 0;
    }
    int added = 0;
    for (auto& t : qt->tracks) {
        std::string label = caudio::app::addedLabel(t);
        if (seen.contains(t.id)) {
            caudio::println(std::cerr, "already in queue: {}", label);
        } else {
            caudio::println("Added {}", label);
            seen.insert(t.id);
            ++added;
        }
    }
    return added;
}

void countLine(int added) {
    if (added == 1)
        caudio::println("1 track added");
    else
        caudio::println("{} tracks added", added);
}

} // namespace caudio::app::detail
