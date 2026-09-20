/**
 * @file output_formatter.hpp
 * @brief Output formatting for CLI results: table and JSON output.
 * @ingroup caudio_client
 */
#pragma once

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

#include "caudio/utils/utils.hpp"
#include "cli/shared/command.hpp"
#include "cli/shared/result.hpp"
#include "cli/shared/protocol.hpp"
#include "cli/config.hpp"
#include "caudio/engine/engine.hpp"
#include "caudio/db/db_types.hpp"

namespace caudio::client {

/**
 * @brief Formats CLI results for human-readable table output or JSON.
 *
 * Provides print() for formatted output and printWithStatus() for exit code handling.
 * Supports all Result variant types: Status, QueueTracks, VolumeInfo, LibraryStats, etc.
 * JSON mode uses nlohmann::json pretty-printing (2-space indent).
 */
class OutputFormatter {
    bool json_{false};

    static std::string formatTime(double secs);
    static std::string playbackStateToString(caudio::engine::PlaybackState s);
    static std::string repeatModeToString(caudio::engine::RepeatMode m);
    static std::string truncateField(const std::string& s, std::size_t maxLen = 40);

  public:
    /**
     * @brief Construct formatter.
     * @param json If true, output JSON; otherwise formatted table/text.
     */
    explicit OutputFormatter(bool json = false) : json_(json) {}

    /**
     * @brief Print a Result to an output stream.
     * @param r Result variant to format.
     * @param os Output stream (stdout/stderr).
     *
     * In JSON mode: pretty-prints entire Result as JSON.
     * In table mode: dispatches to type-specific formatter for each Result alternative.
     */
    void print(const caudio::cli::Result& r, std::ostream& os) const;

    /**
     * @brief Print Result with automatic stderr/stdout routing and exit code.
     * @param r Result to print.
     * @param out Output stream for success (default stdout).
     * @param err Output stream for errors (default stderr).
     * @return 0 on success, 1 if Result holds Error variant.
     *
     * Routes Error variants to err stream, others to out stream.
     * Suitable for CLI main() to return appropriate exit code.
     */
    int printWithStatus(const caudio::cli::Result& r, std::ostream& out, std::ostream& err) const;
};

} // namespace caudio::client