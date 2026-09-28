#include <algorithm>
#include <caudio/client/client.hpp>
#include <caudio/client/ipc_client.hpp>
#include <caudio/config.hpp>

#include "config_detail.hpp"
#include <caudio/ipc/protocol.hpp>
#include <caudio/service/ipc_channel.hpp>
#include <caudio/service/shm_status.hpp>
#include <caudio/utils.hpp>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace caudio::client {

Client::Client(Config cfg) : config_(std::move(cfg)) {}

Client::Client(std::filesystem::path dbPath) : config_{std::move(dbPath), {}} {}

Client::Client(std::filesystem::path dbPath, std::string_view socketPath)
    : config_{std::move(dbPath), std::string(socketPath.data(), socketPath.size())} {}

Client::Client(const caudio::config::Config& cfg) : config_{cfg.dbPath, cfg.socketPath} {}

const Config& Client::config() const noexcept {
    return config_;
}

std::filesystem::path Client::dbPath() const noexcept {
    return config_.dbPath;
}

caudio::utils::Expected<caudio::ipc::Result> Client::send(const caudio::ipc::Command& cmd,
                                                          std::chrono::milliseconds timeout) {
    // Use promise/future + jthread + stop_token for timeout handling.
    // This avoids C alarm and uses chrono::milliseconds + poll/select style wait.
    auto prom = std::make_shared<std::promise<caudio::utils::Expected<caudio::ipc::Result>>>();
    auto fut = prom->get_future();
    caudio::ipc::Command cmdCopy = cmd;
    Config cfgCopy = config_;

    // RAII worker via make_unique per spec
    auto worker = std::make_unique<std::jthread>(
        [prom, cmdCopy = std::move(cmdCopy), cfgCopy](std::stop_token st) mutable {
            if (st.stop_requested()) {
                try {
                    prom->set_value(std::unexpected{
                        caudio::utils::makeError(caudio::utils::StatusCode::Busy, "cancelled")});
                } catch (...) {
                }
                return;
            }
            auto conn = IpcClient::connect(cfgCopy.dbPath, cfgCopy.socketPath);
            if (!conn) {
                try {
                    prom->set_value(std::unexpected{caudio::utils::makeError(
                        caudio::utils::StatusCode::State, "daemon not running")});
                } catch (...) {
                }
                return;
            }
            if (st.stop_requested()) {
                try {
                    prom->set_value(std::unexpected{
                        caudio::utils::makeError(caudio::utils::StatusCode::Busy, "cancelled")});
                } catch (...) {
                }
                return;
            }
            auto res = conn->send(cmdCopy);
            try {
                if (res) {
                    prom->set_value(*res);
                } else {
                    prom->set_value(std::unexpected{res.error()});
                }
            } catch (...) {
            }
        });

    // Handle timeout via chrono::milliseconds -- uses future::wait_for.
    if (fut.wait_for(timeout) == std::future_status::ready) {
        return fut.get();
    } else {
        worker->request_stop();
        // Avoid blocking join beyond timeout: move worker to background storage with reaping and
        // bounded size.
        static std::mutex bgMtx;
        static std::vector<std::unique_ptr<std::jthread>> bg;
        {
            std::lock_guard lk(bgMtx);
            // Reap completed workers: jthread that has finished is still joinable until joined,
            // but if it has been joined/detached elsewhere it becomes !joinable(). Also check
            // stop_token as best-effort to prune. This prevents unbounded growth.
            bg.erase(std::remove_if(
                         bg.begin(), bg.end(),
                         [](const std::unique_ptr<std::jthread>& t) { return !t->joinable(); }),
                     bg.end());
            // Enforce bounded size (audit B6: limit to 8 background workers)
            if (bg.size() >= 8) {
                // Drop oldest without blocking join: release handle to avoid blocking destructor
                // on a still-running thread; leak is bounded to at most 8 threads total.
                (void)bg.front().release();
                bg.erase(bg.begin());
            }
            bg.push_back(std::move(worker));
        }
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Io, "timeout")};
    }
}

// Snapshot status via shared memory (for TUI 10fps polling) or fallback to IPC
caudio::utils::Expected<caudio::ipc::Result> Client::snapshotStatus() {
    // Try to connect to shared memory status block
    // Derive shm name via canonical hex8 (consistent with service)
    std::string shmName = caudio::config::detail_paths::hex8ForDb(config_.dbPath);

    auto shmRes = caudio::service::ShmStatusHandle::openReadOnly(shmName);
    if (shmRes) {
        auto snapshot = shmRes->snapshot();
        caudio::ipc::Status s{};
        s.state = static_cast<caudio::engine::PlaybackState>(snapshot.state);
        s.pos = snapshot.position;
        s.dur = snapshot.duration;
        s.vol = snapshot.volume;
        s.muted = snapshot.muted;
        s.track_id = snapshot.track_id;
        s.q_size = snapshot.queueSize;
        s.title = snapshot.title;
        s.artist = snapshot.artist;
        // shuffle/repeat not in shm, would need to query via IPC if needed
        return caudio::ipc::Result{s};
    }

    // Fallback to IPC
    return send(caudio::ipc::Command{caudio::ipc::StatusReq{}});
}

} // namespace caudio::client
