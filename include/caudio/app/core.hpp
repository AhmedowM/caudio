#pragma once

/**
 * @file core.hpp
 * @brief Application orchestrator shared by all frontends.
 * @ingroup caudio_app
 * @details `App` owns the behavior moved out of the CLI shell: daemon
 * lifecycle (spawn, readiness, shutdown), transport policy (autostart,
 * retries) and, over time, every command dispatch path. Frontends (the CLI
 * shell today, TUIs tomorrow) keep only their own syntax and rendering and
 * call into this class.
 *
 * Phase 0 note: rendering still happens inside these methods (moved verbatim
 * from the shell); Phase 1 converts them to return data for callers to
 * render. Exit-code convention is CLI-compatible throughout: 0 ok, 1
 * runtime failure.
 *
 * Thread safety: thread-safe for concurrent calls; daemon interactions go
 * through the IPC client, which serializes per connection.
 */

#include <caudio/config.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/utils/error.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace caudio::app {

/**
 * @brief Command outcome data for frontends to render.
 * @ingroup caudio_app
 * @details handlers return this instead of printing. Frontends
 * render via OutputFormatter (bare/JSON), print `line`, or stay silent:
 * result plus line is a confirmation, result alone bare-renders, line
 * alone with toStderr is a warning (exit still 0), neither is silent
 * success.
 */
struct Outcome {
    std::optional<caudio::ipc::Result> result;
    std::optional<std::string> line;
    bool toStderr = false;
    static std::expected<Outcome, caudio::utils::Error> warn(std::string message);
};
/** @brief Handler return: outcome data, or the error to render. */
using AppResult = std::expected<Outcome, caudio::utils::Error>;

/**
 * @brief Application orchestrator bound to one config (one database).
 * @ingroup caudio_app
 * @see caudio::config::Config
 */
class App {
  public:
    /**
     * @brief Binds the orchestrator to a config.
     * @ingroup caudio_app
     * @param cfg Resolved config (db/socket/log paths); copied.
     */
    explicit App(caudio::config::Config cfg);
    /** @brief Dtor. @ingroup caudio_app */
    ~App();

    /**
     * @brief Replaces the bound config (after frontend flag parsing).
     * @ingroup caudio_app
     * @param cfg Fully resolved config; copied.
     * @details Frontends construct App before parsing their own flags, so
     * the bound config is stale until they call this once parsing plus
     * config-file reload plus socket derivation are done. Every daemon
     * interaction below must observe the final config.
     */
    void setConfig(caudio::config::Config cfg);

    /**
     * @brief Sets argv[0] for daemon re-spawn path resolution.
     * @ingroup caudio_app
     * @param argv0 Executable path as invoked (POSIX fallback only).
     * @details Frontends call this once from their entry point before any
     * spawn; on Windows the module path is used instead.
     */
    void setArgv0(std::string argv0);

    /**
     * @brief Starts the daemon (foreground service or background spawn).
     * @ingroup caudio_app
     * @param foreground Run the service in-process and block (child path).
     * @param quiet Suppress informational output (JSON/autostart callers).
     * @return 0 when the daemon answers a status round-trip, 1 otherwise.
     * @details Background path waits out a previous daemon's teardown,
     * spawns once, then requires a full StatusReq round-trip (not just a
     * connect) within budget before reporting success.
     */
    int startDaemon(bool foreground, bool quiet = false);

    /**
     * @brief Stops the daemon and waits for its teardown to complete.
     * @ingroup caudio_app
     * @return 0 when the daemon is unreachable and its pid file is gone.
     */
    int shutdownDaemon();

    /**
     * @brief Spawns a detached daemon child for the given config.
     * @ingroup caudio_app
     * @param cfg Config selecting db/socket paths for the child.
     * @return Success, or the OS error code when spawning failed.
     * @details Windows uses DETACHED_PROCESS; POSIX double-detaches via
     * fork/setsid (single fork + wait-free, like the historical behavior).
     * The child's readiness is NOT waited on here -- see startDaemon().
     */
    std::expected<void, std::uint32_t> spawnDaemon(const caudio::config::Config& cfg);

    /**
     * @brief Derives the pid-file path for the bound config.
     * @ingroup caudio_app
     * @return Canonical pid path, or a db-adjacent fallback.
     */
    std::filesystem::path pidPathForConfig() const;

    /**
     * @brief Sends a command and normalizes server-side errors.
     * @ingroup caudio_app
     * @param cmd IPC command to send.
     * @param timeout Per-attempt timeout (default 2 s).
     * @return Server result, or the transport/server error.
     * @details Unwraps `Error` results into unexpected, so callers handle
     * transport and application errors uniformly.
     */
    std::expected<caudio::ipc::Result, caudio::utils::Error>
    sendRaw(const caudio::ipc::Command& cmd,
            std::chrono::milliseconds timeout = std::chrono::milliseconds{2000});

    /**
     * @brief Prints an error plus the start hint when relevant.
     * @ingroup caudio_app
     * @return Always 1 (CLI exit convention, kept for renderer parity).
     */
    int printErr(const caudio::utils::Error& e);

    /**
     * @brief Prints a result as JSON.
     * @ingroup caudio_app
     * @return Always 0.
     */
    int printJson(const caudio::ipc::Result& r);

    /**
     * @brief Sends a command and renders the result (text or JSON).
     * @ingroup caudio_app
     */
    int sendViaClient(const caudio::ipc::Command& cmd, bool asJson);

    /**
     * @brief Sends a play-like command, autostarting a down daemon.
     * @ingroup caudio_app
     * @param cmd IPC command to send.
     * @param quietAutostart Suppress autostart chatter (JSON callers).
     */
    std::expected<caudio::ipc::Result, caudio::utils::Error>
    sendPlay(const caudio::ipc::Command& cmd, bool quietAutostart);

    /**
     * @brief Renders a result as a one-line confirmation (or JSON).
     * @ingroup caudio_app
     * @param line Text line for the success path.
     */
    int confirm(std::expected<caudio::ipc::Result, caudio::utils::Error>&& res, bool asJson,
                const std::string& line);

    /**
     * @brief Pairs a result with its success line, passing errors through.
     * @ingroup caudio_app
     * @param res Daemon outcome to wrap.
     * @param line Human success line; nullopt means bare-render the result.
     * @return Outcome data for the frontend; errors surface unchanged.
     */
    AppResult confirm(std::expected<caudio::ipc::Result, caudio::utils::Error>&& res,
                      std::optional<std::string> line = std::nullopt);

    /**
     * @brief Renders a transport result as a one-line confirmation.
     * @ingroup caudio_app
     * @details Same shape as confirm(); kept separate so transport wording
     * can evolve without touching generic call sites.
     */
    int confirmTransport(std::expected<caudio::ipc::Result, caudio::utils::Error>&& res,
                         bool asJson, const std::string& line);

    /**
     * @brief Human track label for the last transport result.
     * @ingroup caudio_app
     * @return Track label, or "unknown track" for non-status results.
     */
    std::string transportWho(const std::expected<caudio::ipc::Result, caudio::utils::Error>& res);

    /**
     * @brief Shared play flow (used by play, and by resume when stopped).
     * @ingroup caudio_app
     * @param asJson Render raw JSON instead of the one-line confirmation.
     * @details Probes paused state first so the wording (resumed vs fresh)
     * matches; autostarts a down daemon via sendPlay().
     */
    int doPlay(bool asJson);

    /**
     * @brief Plays files immediately in a new queue.
     * @ingroup caudio_app
     * @param files Pre-expanded file list (glob expansion stays in frontends).
     * @param save Keep the queue instead of purging it on shutdown.
     */
    int playFiles(const std::vector<std::string>& files, bool save);

    /** @brief Pause playback (warns instead of failing when idle). @ingroup caudio_app */
    int pause(bool asJson);
    /**
     * @brief Resume playback (forgiving: warns when playing, plays from
     * cursor when stopped).
     * @ingroup caudio_app
     */
    int resume(bool asJson);
    /** @brief Restart the current track. @ingroup caudio_app */
    int restart(bool asJson);
    /** @brief Stop playback. @ingroup caudio_app */
    int stop(bool asJson);
    /** @brief Advance to the next track. @ingroup caudio_app */
    int next(bool asJson);
    /** @brief Move to the previous track (warns at queue start). @ingroup caudio_app */
    int prev(bool asJson);
    /**
     * @brief Seek to an absolute position, or by delta when relative.
     * @ingroup caudio_app
     * @param target Seconds (absolute) or delta (relative to now).
     * @param isRelative Resolve target against the current position first.
     * @param asJson Render raw JSON instead of staying silent.
     * @details Argument grammar (mm:ss, +/-) is parsed by frontends; the
     * relative flag arrives pre-detected.
     */
    int seek(double target, bool isRelative, bool asJson);

    /**
     * @brief Lists queues (id, name, track counts, active/temp markers).
     * @ingroup caudio_app
     */
    int queues(bool asJson);

    /**
     * @brief Lists tracks in the active queue (added or playback order).
     * @ingroup caudio_app
     * @param order "added" for insertion order, anything else for playback.
     */
    int queueTracks(const std::string& order, bool asJson);

    /** @brief Switches the active queue. @ingroup caudio_app */
    int queueSwitch(std::int64_t qid, bool asJson);

    /** @brief Creates a queue container. @ingroup caudio_app */
    int queueCreate(const std::string& name, bool asJson);

    /** @brief Deletes a queue container (active queue guarded). @ingroup caudio_app */
    int queueDelete(std::int64_t qid, bool asJson);

    /**
     * @brief Adds to the queue via playlist, id/search, or paths.
     * @ingroup caudio_app
     * @param paths PATH/glob/folder selectors (PATH mode only).
     * @param id Library id (id mode) or search query (search mode).
     * @param search Treat id as an FTS query instead of a numeric id.
     * @param playlist Source playlist id (playlist mode, 0 = off).
     * @param replace Overwrite the active queue first (playlist mode only).
     * @param recursive Descend into folders (PATH mode only).
     * @details Mode validation travels with the handler, verbatim from the
     * shell; glob expansion uses the shared paths utilities.
     */
    int queueAdd(const std::vector<std::string>& paths, const std::string& id, bool search,
                 std::int64_t playlist, bool replace, bool recursive, bool asJson);

    /**
     * @brief Removes from the queue by id, position, or paths.
     * @ingroup caudio_app
     * @details Selectors are exclusive; PATH mode matches
     * separator-insensitively against an insertion-order snapshot.
     */
    int queueRemove(const std::string& id, const std::string& pos,
                    const std::vector<std::string>& paths, bool recursive, bool asJson);

    /** @brief Moves a track within the queue. @ingroup caudio_app */
    int queueMove(std::size_t from, std::size_t to, bool asJson);

    /** @brief Clears the active queue. @ingroup caudio_app */
    int queueClear(bool asJson);

    /**
     * @brief Sets shuffle (on/off/empty toggles to resulting state).
     * @ingroup caudio_app
     */
    int queueShuffle(const std::string& mode, bool asJson);

    /**
     * @brief Sets repeat (off/one/all; empty cycles off->all->one->off).
     * @ingroup caudio_app
     */
    int queueRepeat(const std::string& mode, bool asJson);

    /**
     * @brief Lists playlists.
     * @ingroup caudio_app
     */
    int playlistList(bool asJson);

    /**
     * @brief Lists tracks on a playlist.
     * @ingroup caudio_app
     */
    int playlistTracks(std::int64_t pid, bool asJson);

    /**
     * @brief Creates an empty playlist.
     * @ingroup caudio_app
     */
    int playlistCreate(const std::string& name, bool asJson);

    /**
     * @brief Adds ids/paths to a playlist (resolving files via the library).
     * @ingroup caudio_app
     * @param pid Target playlist id.
     * @param ids Library id strings (validated numeric here).
     * @param paths PATH/glob/folder selectors, expanded via paths utilities.
     * @param recursive Descend into folders (PATH mode only).
     */
    int playlistAdd(std::int64_t pid, const std::vector<std::string>& ids,
                    const std::vector<std::string>& paths, bool recursive, bool asJson);

    /**
     * @brief Loads a playlist into a queue (optionally replacing/playing).
     * @ingroup caudio_app
     */
    int playlistLoad(std::int64_t pid, bool play, bool replace, bool asJson);

    /**
     * @brief Saves a queue as a playlist.
     * @ingroup caudio_app
     * @param qid Source queue id, or nullopt for the active queue.
     */
    int playlistSave(const std::string& name, std::optional<std::int64_t> qid, bool asJson);

    /** @brief Deletes a playlist. @ingroup caudio_app */
    int playlistDelete(std::int64_t pid, bool asJson);
    /** @brief Renames a playlist. @ingroup caudio_app */
    int playlistRename(std::int64_t pid, const std::string& name, bool asJson);

    /**
     * @brief Exports a playlist to an m3u/pls/json file.
     * @ingroup caudio_app
     */
    int playlistExport(std::int64_t pid, const std::string& path, const std::string& format);

    /**
     * @brief Imports a playlist file.
     * @ingroup caudio_app
     * @param name Playlist name, or nullopt to derive from the file.
     */
    int playlistImport(const std::string& path, std::optional<std::string> name, bool asJson);

    /**
     * @brief Scans a path into the library.
     * @ingroup caudio_app
     * @param path Scan root, or nullopt for the configured library path.
     * @param fullHash Fingerprint full content instead of sampling.
     */
    int libraryScan(std::optional<std::string> path, bool fullHash, bool asJson);

    /**
     * @brief Searches the library (filenames included).
     * @ingroup caudio_app
     */
    int librarySearch(const std::string& query, int limit, bool asJson);

    /**
     * @brief Shows library stats, optionally with queue/playlist breakdowns.
     * @ingroup caudio_app
     * @param mostPlayed Top-N threshold (>= 0 enables detailed mode).
     * @param queues Queue selectors ("all" or numeric ids).
     */
    int libraryStats(int mostPlayed, const std::vector<std::string>& queues,
                     const std::vector<std::int64_t>& playlists, bool asJson);

    /**
     * @brief Lists library tracks with optional filters.
     * @ingroup caudio_app
     * @details Empty strings mean no filter; translated to nullopts here so
     * frontends pass plain values.
     */
    int libraryList(const std::string& query, int limit, int offset, const std::string& artist,
                    const std::string& album, const std::string& genre, bool asJson);

    /** @brief Adds a file or directory to the library. @ingroup caudio_app */
    int libraryAdd(const std::string& path, bool recursive, bool asJson);

    /**
     * @brief Removes a track from the library (id or path).
     * @ingroup caudio_app
     * @details Resolves a human label first so the confirmation names it.
     */
    int libraryRemove(const std::string& query, bool asJson);

    /**
     * @brief Edits one track tag field (database row).
     * @ingroup caudio_app
     */
    int tagEdit(std::int64_t id, const std::string& field, const std::string& value, bool asJson);

    /**
     * @brief Reads track tags, optionally selecting a single field.
     * @ingroup caudio_app
     * @param field Empty for the full record, otherwise one of the known
     * fields (validated here; unknown names fail fast).
     */
    int tagGet(std::int64_t id, const std::string& field, bool asJson);

    /** @brief Lists playback history entries. @ingroup caudio_app */
    AppResult historyList(int limit);

    /** @brief Clears all playback history. @ingroup caudio_app */
    AppResult historyClear();

    /** @brief Lists audio output devices. @ingroup caudio_app */
    AppResult deviceList();

    /** @brief Sets the default audio output device. @ingroup caudio_app */
    AppResult deviceSet(const std::string& id);

    /**
     * @brief Checks an audio device is available.
     * @ingroup caudio_app
     * @param id Device id, or nullopt for the default device.
     */
    AppResult deviceTest(std::optional<std::string> id);

    /**
     * @brief Reads a config value from the local file (no daemon needed).
     * @ingroup caudio_app
     */
    int configGet(const std::string& key, bool asJson);

    /** @brief Sets a config value (daemon-side). @ingroup caudio_app */
    int configSet(const std::string& key, const std::string& value);

    /** @brief Lists config entries. @ingroup caudio_app */
    int configList(bool asJson);

    /** @brief Exports config to a file. @ingroup caudio_app */
    int configExport(const std::string& path, bool asJson);

    /** @brief Imports config from a file (validated first). @ingroup caudio_app */
    int configImport(const std::string& path, bool asJson);

    /**
     * @brief Resets config to defaults (one key or all).
     * @ingroup caudio_app
     * @param key Key to reset, or nullopt for everything.
     */
    int configReset(std::optional<std::string> key, bool asJson);

    /**
     * @brief Plays a file ephemerally without touching the daemon/queue.
     * @ingroup caudio_app
     * @details Blocks until playback finishes. Used for quick audition.
     */
    int previewFile(const std::string& file);

  private:
    caudio::config::Config config_; ///< Bound config (db/socket/log paths).
    std::string argv0_;             ///< Executable path for POSIX re-spawn.
};

} // namespace caudio::app
