#include <caudio/db/db_types.hpp>
#include <caudio/db/scan.hpp>
#include <caudio/db/search.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/service/service_core.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/result.hpp>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <db/fingerprint.hpp>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "service_audio.hpp"

namespace caudio::service {

using namespace caudio::ipc;
using caudio::ipc::QueueAdd;
using caudio::ipc::QueueClear;
using caudio::ipc::QueueList;
using caudio::ipc::QueueMove;
using caudio::ipc::QueueQueues;
using caudio::ipc::QueueRemove;
using caudio::ipc::QueueRepeat;
using caudio::ipc::QueueShuffle;
using caudio::ipc::QueueSwitch;

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::QueueList& ql) {
    if (ql.order != "playback" && ql.order != "added" && !ql.order.empty())
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg,
                                                        "queue list: invalid order (added|playback)")};
    int64_t qid = engine_->activeQueueId();
    // validation: queue exists
    {
        auto q = db_->getQueue(qid);
        if (!q)
            return std::unexpected{q.error()};
    }
    auto items = db_->queueList(qid);
    if (!items)
        return std::unexpected{items.error()};
    std::vector<caudio::db::Track> tracks;
    tracks.reserve(items->size());
    for (auto& it : *items) {
        auto tr = db_->getTrack(it.track_id);
        if (tr)
            tracks.push_back(std::move(*tr));
    }
    if (ql.order != "added") {
        // Playback (shuffle) order: perm holds queue positions. A stale perm
        // (queue mutated after shuffle) contributes its valid entries first;
        // anything missing follows in insertion order.
        auto perm = engine_->shufflePermFor(qid);
        if (!perm.empty()) {
            std::vector<caudio::db::Track> ordered;
            ordered.reserve(tracks.size());
            std::vector<char> seen(tracks.size(), 0);
            for (int64_t p : perm) {
                if (p >= 0 && (std::size_t)p < tracks.size() &&
                    !seen[(std::size_t)p]) {
                    ordered.push_back(tracks[(std::size_t)p]);
                    seen[(std::size_t)p] = 1;
                }
            }
            for (std::size_t i = 0; i < tracks.size(); ++i) {
                if (!seen[i])
                    ordered.push_back(tracks[i]);
            }
            tracks = std::move(ordered);
        }
    }
    return Result{QueueTracks{std::move(tracks)}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::QueueQueues&) {
    auto qs = db_->listQueues();
    if (!qs)
        return std::unexpected{qs.error()};
    int64_t active = engine_->activeQueueId();
    caudio::ipc::Queues out{};
    for (auto& q : *qs) {
        auto items = db_->queueList(q.id);
        std::size_t n = items ? items->size() : 0;
        out.entries.push_back(
            caudio::ipc::QueueEntry{q.id, q.name, n, q.id == active});
    }
    return Result{std::move(out)};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::QueueSwitch& qs) {
    auto q = db_->getQueue(qs.qid);
    if (!q)
        return std::unexpected{q.error()};
    auto sw = engine_->switchQueue(qs.qid);
    if (!sw)
        return std::unexpected{sw.error()};
    updateShmStatus();
    return statusResult();
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::QueueAdd& qa) {
    int64_t qid = engine_->activeQueueId();
    auto q = db_->getQueue(qid);
    if (!q)
        return std::unexpected{q.error()};
    if (!qa.search) {
        std::filesystem::path p(qa.query);
        std::error_code ec;
        bool exists = std::filesystem::exists(p, ec);
        if (!ec && caudio::db::detail::hasAudioExt(p)) {
            if (!exists) {
                return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound,
                                                                "track not found: " + qa.query)};
            }
            auto fpRes = caudio::db::internal::computeFingerprint(p);
            if (!fpRes)
                return std::unexpected{fpRes.error()};
            caudio::db::Track t;
            t.path = p.generic_string();
            t.fingerprint = *fpRes;
            t.duration = detail::durationFromDecoder(p);
            {
                std::error_code ec2;
                auto sz = std::filesystem::file_size(p, ec2);
                if (!ec2)
                    t.size = static_cast<int64_t>(sz);
                auto ftime = std::filesystem::last_write_time(p, ec2);
                if (!ec2)
                    t.mtime = static_cast<int64_t>(ftime.time_since_epoch().count());
            }
            int64_t newId = 0;
            auto ins = db_->insertTrack(t);
            if (ins) {
                newId = *ins;
                t.id = newId;
            } else {
                if (ins.error().code == caudio::utils::StatusCode::AlreadyExists) {
                    auto existing = db_->findByFingerprint(t.fingerprint);
                    if (existing) {
                        t = std::move(*existing);
                        newId = t.id;
                    } else {
                        auto byPath = db_->findByPath(t.path);
                        if (byPath) {
                            t = std::move(*byPath);
                            newId = t.id;
                        } else {
                            return std::unexpected{ins.error()};
                        }
                    }
                } else {
                    return std::unexpected{ins.error()};
                }
            }
            auto eq = db_->queueEnqueue(qid, newId);
            if (!eq)
                return std::unexpected{eq.error()};
            updateShmStatus();
            std::vector<caudio::db::Track> single;
            single.reserve(1);
            single.push_back(std::move(t));
            return Result{QueueTracks{std::move(single)}};
        }
    }
    std::vector<caudio::db::Track> toAdd;
    if (qa.search) {
        auto sr = caudio::db::search(*db_, qa.query, 50);
        if (!sr)
            return std::unexpected{sr.error()};
        toAdd = std::move(*sr);
    } else {
        // try parse as int id
        bool parsed = false;
        int64_t id = 0;
        try {
            std::string s = qa.query;
            // trim
            s.erase(0, s.find_first_not_of(" \t\n\r"));
            s.erase(s.find_last_not_of(" \t\n\r") + 1);
            if (!s.empty()) {
                // check if all digits (allow leading -)
                bool isNum = true;
                for (std::size_t i = (s[0] == '-' ? 1 : 0); i < s.size(); ++i)
                    if (!std::isdigit((unsigned char)s[i])) {
                        isNum = false;
                        break;
                    }
                if (isNum) {
                    id = std::stoll(s);
                    parsed = true;
                }
            }
        } catch (...) {
        }
        if (parsed && id != 0) {
            auto tr = db_->getTrack(id);
            if (!tr)
                return std::unexpected{tr.error()};
            toAdd.push_back(std::move(*tr));
        } else {
            // try findByPath
            auto tr = db_->findByPath(qa.query);
            if (tr) {
                toAdd.push_back(std::move(*tr));
            } else {
                // fallback to search via FTS (covers LIKE)
                auto sr = caudio::db::search(*db_, qa.query, 50);
                if (!sr)
                    return std::unexpected{sr.error()};
                toAdd = std::move(*sr);
                if (toAdd.empty()) {
                    return std::unexpected{caudio::utils::makeError(
                        caudio::utils::StatusCode::NotFound, "track not found: " + qa.query)};
                }
            }
        }
    }
    // Batch enqueue in single transaction: atomic to concurrent queueList, rollback
    // on failure
    {
        std::vector<int64_t> ids;
        ids.reserve(toAdd.size());
        for (auto& t : toAdd)
            ids.push_back(t.id);
        auto er = db_->queueEnqueueBatch(qid, ids);
        if (!er)
            return std::unexpected{er.error()};
    }
    updateShmStatus();
    return Result{QueueTracks{std::move(toAdd)}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::QueueRemove& qr) {
    int64_t qid = engine_->activeQueueId();
    auto q = db_->getQueue(qid);
    if (!q)
        return std::unexpected{q.error()};
    // parse idOrIndex
    int64_t val = 0;
    bool isNum = false;
    try {
        std::string s = qr.idOrIndex;
        s.erase(0, s.find_first_not_of(" \t\n\r"));
        s.erase(s.find_last_not_of(" \t\n\r") + 1);
        if (!s.empty()) {
            bool allDigit = true;
            std::size_t off = (s[0] == '-' ? 1 : 0);
            for (std::size_t i = off; i < s.size(); ++i)
                if (!std::isdigit((unsigned char)s[i])) {
                    allDigit = false;
                    break;
                }
            if (allDigit) {
                val = std::stoll(s);
                isNum = true;
            }
        }
    } catch (...) {
    }
    if (!isNum) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "invalid idOrIndex")};
    }
    // try as position first
    auto rm = db_->queueRemove(qid, val);
    if (!rm) {
        // if not found as position, try as track_id lookup
        if (rm.error().code == caudio::utils::StatusCode::NotFound) {
            auto items = db_->queueList(qid);
            if (!items)
                return std::unexpected{items.error()};
            bool found = false;
            int64_t pos = -1;
            for (auto& it : *items)
                if (it.track_id == val) {
                    pos = it.position;
                    found = true;
                    break;
                }
            if (!found)
                return std::unexpected{rm.error()};
            auto rm2 = db_->queueRemove(qid, pos);
            if (!rm2)
                return std::unexpected{rm2.error()};
        } else {
            return std::unexpected{rm.error()};
        }
    }
    auto items = db_->queueList(qid);
    if (!items)
        return std::unexpected{items.error()};
    std::vector<caudio::db::Track> tracks;
    for (auto& it : *items) {
        auto tr = db_->getTrack(it.track_id);
        if (tr)
            tracks.push_back(std::move(*tr));
    }
    updateShmStatus();
    return Result{QueueTracks{std::move(tracks)}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::QueueMove& qm) {
    // QueueMove: reorder within queue via playlistReorder? For queue we lack direct
    // move. Simulate via remove+enqueue: fetch items, reorder vector, clear and
    // re-enqueue
    int64_t qid = engine_->activeQueueId();
    auto q = db_->getQueue(qid);
    if (!q)
        return std::unexpected{q.error()};
    auto items = db_->queueList(qid);
    if (!items)
        return std::unexpected{items.error()};
    if (qm.from >= items->size() || qm.to >= items->size()) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "move out of range")};
    }
    // collect track_ids in order
    std::vector<int64_t> ids;
    ids.reserve(items->size());
    for (auto& it : *items)
        ids.push_back(it.track_id);
    int64_t mv = ids[qm.from];
    ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(qm.from));
    ids.insert(ids.begin() + static_cast<std::ptrdiff_t>(qm.to), mv);
    // Transactional clear+enqueue: single BEGIN IMMEDIATE/COMMIT so concurrent
    // queueList never sees empty
    {
        auto r = db_->queueReplaceAll(qid, ids);
        if (!r)
            return std::unexpected{r.error()};
    }
    auto nitems = db_->queueList(qid);
    if (!nitems)
        return std::unexpected{nitems.error()};
    std::vector<caudio::db::Track> tracks;
    for (auto& it : *nitems) {
        auto tr = db_->getTrack(it.track_id);
        if (tr)
            tracks.push_back(std::move(*tr));
    }
    updateShmStatus();
    return Result{QueueTracks{std::move(tracks)}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::QueueClear&) {
    int64_t qid = engine_->activeQueueId();
    auto q = db_->getQueue(qid);
    if (!q)
        return std::unexpected{q.error()};
    auto r = db_->queueClear(qid);
    if (!r)
        return std::unexpected{r.error()};
    updateShmStatus();
    return Result{QueueTracks{{}}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::QueueShuffle& qs) {
    bool on;
    if (qs.on.has_value()) {
        on = *qs.on;
    } else {
        // toggle: get current state and flip
        on = !engine_->shuffle();
    }
    auto r = engine_->setShuffle(on);
    if (!r)
        return std::unexpected{r.error()};
    updateShmStatus();
    return statusResult();
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::QueueRepeat& qr) {
    if (qr.mode.has_value()) {
        auto r = engine_->setRepeat(*qr.mode);
        if (!r)
            return std::unexpected{r.error()};
    }
    // if no arg provided, just show current (statusResult already includes it)
    updateShmStatus();
    return statusResult();
}

} // namespace caudio::service
