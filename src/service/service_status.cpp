#include "service_status.hpp"

#include <blake3.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#endif

#include <caudio/config.hpp>
#include <caudio/db.hpp>
#include <caudio/engine.hpp>
#include <caudio/ipc.hpp>
#include <caudio/player.hpp>
#include <caudio/utils.hpp>
#include <caudio/version.hpp>
#include <caudio/windows_fwd.hpp>

namespace caudio::service::detail {
std::expected<caudio::ipc::Status, caudio::utils::Error> buildStatus(caudio::engine::Engine& eng,
                                                                     caudio::db::Database& db) {
    caudio::ipc::Status s{};
    s.version = std::string(caudio::version::version());
    s.state = eng.state();
    s.pos = eng.position();
    s.dur = eng.duration();
    s.vol = eng.volume();
    s.muted = eng.volume() == 0.0f;
    s.shuffle = eng.shuffle();
    s.repeat = eng.repeat();
    s.track_id = eng.currentTrackId();
    if (s.track_id != 0) {
        auto tr = db.getTrack(s.track_id);
        if (tr) {
            s.title = tr->title;
            s.artist = tr->artist;
            s.path = tr->path;
        }
    }
    try {
        int64_t activeQ = eng.activeQueueId();
        // O(1) size via COUNT(*) + O(log n) position lookup (C-3): the old
        // code materialized the full queue vector and scanned it linearly
        // on every status call (including --watch each second).
        s.q_size = db.queueCountLocked(activeQ);
        s.q_idx = 0;
        if (s.track_id != 0) {
            if (auto idx = db.queueIndexOf(activeQ, s.track_id); idx)
                s.q_idx = *idx;
        }
    } catch (...) {
    }
    return s;
}

} // namespace caudio::service::detail
