#include "cli/client/client_impl.hpp"

#include <caudio/utils/utils.hpp>
#include <cli/shared/protocol.hpp>
#include <cli/config.hpp>
#include <cli/service/ipc_channel.hpp>
#include <cli/client/ipc_client.hpp>
#include <cli/service/shm_status.hpp>
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

Client::Client(const caudio::cli::Config& cfg) : config_{cfg.dbPath, cfg.socketPath} {}

const Config& Client::config() const noexcept {
    return config_;
}

std::filesystem::path Client::dbPath() const noexcept {
    return config_.dbPath;
}

caudio::utils::Expected<caudio::cli::Result>
Client::send(const caudio::cli::Command& cmd,
             std::chrono::milliseconds timeout) {
    // Use promise/future + jthread + stop_token for timeout handling.
    // This avoids C alarm and uses chrono::milliseconds + poll/select style wait.
    auto prom = std::make_shared<std::promise<caudio::utils::Expected<caudio::cli::Result>>>();
    auto fut = prom->get_future();
    caudio::cli::Command cmdCopy = cmd;
    Config cfgCopy = config_;

    // RAII worker via make_unique per spec
    auto worker = std::make_unique<std::jthread>([prom, cmdCopy = std::move(cmdCopy),
                                                  cfgCopy](std::stop_token st) mutable {
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
                prom->set_value(std::unexpected{caudio::utils::Error{
                    caudio::utils::StatusCode::State,
                    std::string_view{"daemon not running — run 'caudio start'"}}});
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

    // Handle timeout via chrono::milliseconds — uses future::wait_for.
    if (fut.wait_for(timeout) == std::future_status::ready) {
        return fut.get();
    } else {
        worker->request_stop();
        // Avoid blocking join beyond timeout: move worker to background storage
        static std::mutex bgMtx;
        static std::vector<std::unique_ptr<std::jthread>> bg;
        {
            std::lock_guard lk(bgMtx);
            bg.push_back(std::move(worker));
        }
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Io, "timeout")};
    }
}

// Snapshot status via shared memory (for TUI 10fps polling) or fallback to IPC
caudio::utils::Expected<caudio::cli::Result> Client::snapshotStatus() {
    // Try to connect to shared memory status block
    // Derive hash from dbPath for shm name
    std::string dbStr = config_.dbPath.generic_string();
    std::size_t hash = std::hash<std::string>{}(dbStr);
    std::string shmName = std::to_string(hash);

    auto shmRes = caudio::service::ShmStatusHandle::openReadOnly(shmName);
    if (shmRes) {
        auto snapshot = shmRes->snapshot();
        caudio::cli::Status s{};
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
        return caudio::cli::Result{s};
    }

    // Fallback to IPC
    return send(caudio::cli::Command{caudio::cli::StatusReq{}});
}

} // namespace caudio::client