#include <sqlite3.h>

#include <caudio/db/core.hpp>
#include <caudio/db/json.hpp>
#include <caudio/db/types.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/json.hpp>
#include <caudio/utils/result.hpp>
#include <cstddef>
#include <cstdint>
#include <db/statement.hpp>
#include <db/transaction.hpp>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <mutex>
#include <span>
#include <string>
#include <utility>

namespace caudio::db {

using caudio::utils::Json;

std::expected<void, caudio::utils::Error> exportJson(Database& db,
                                                     const std::filesystem::path& outPath) {
    auto tracks = db.listTracks(nullptr);
    if (!tracks)
        return std::unexpected{tracks.error()};
    Json root;
    root["tracks"] = Json::array();
    for (auto& t : *tracks) {
        root["tracks"].push_back(caudio::utils::trackToJson(t));
    }
    std::ofstream f(outPath, std::ios::binary);
    if (!f)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Io, "cannot open output")};
    f << root.dump(2);
    if (!f)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Io, "write failed")};
    return {};
}

std::expected<void, caudio::utils::Error> importJson(Database& db,
                                                     const std::filesystem::path& inPath) {
    std::ifstream f(inPath, std::ios::binary);
    if (!f)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Io, "cannot open input")};
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (content.empty())
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Corrupt, "empty file")};
    Json root;
    try {
        root = Json::parse(content);
    } catch (const std::exception& e) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Corrupt, e.what())};
    }
    if (!root.contains("tracks"))
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Corrupt, "missing tracks array")};
    const Json tracks = root["tracks"];
    if (!tracks.isArray()) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Corrupt, "missing tracks array")};
    }
    std::unique_lock lk{db.mutex()};
    sqlite3* h = db.handleLocked();
    if (!h)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
    auto txRes = DbTransaction::begin(h);
    if (!txRes)
        return std::unexpected{txRes.error()};
    DbTransaction tx = std::move(*txRes);
    bool corrupt = false;
    for (std::size_t ti = 0, tn = tracks.size(); ti < tn; ++ti) {
        auto jt = tracks.at(ti);
        if (!jt) {
            corrupt = true;
            break;
        }
        auto tr = caudio::utils::trackFromJson(*jt);
        if (!tr) {
            corrupt = true;
            break;
        }
        caudio::types::Track t = *tr;
        SqliteStatement stmt;
        if (auto e =
                stmt.prepare(h, "INSERT INTO tracks (fingerprint, path, size, mtime, duration, "
                                "sample_rate, channels, bitrate, title, artist, album, "
                                "album_artist, genre, year, track_num, disc_num, "
                                "cover_art_path, rating, play_count, last_played, date_added, "
                                "last_scanned, dirty, library_id, deleted_at) VALUES "
                                "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
            !e) {
            corrupt = true;
            break;
        }
        auto fpSpan = std::span<const std::byte>{
            reinterpret_cast<const std::byte*>(t.fingerprint.data()), t.fingerprint.size()};
        stmt.bindBlob(1, fpSpan);
        stmt.bindText(2, t.path);
        stmt.bindInt(3, t.size);
        stmt.bindInt(4, t.mtime);
        stmt.bindDouble(5, t.duration);
        stmt.bindInt(6, t.sample_rate);
        stmt.bindInt(7, t.channels);
        stmt.bindInt(8, t.bitrate);
        stmt.bindText(9, t.title);
        stmt.bindText(10, t.artist);
        stmt.bindText(11, t.album);
        stmt.bindText(12, t.album_artist);
        stmt.bindText(13, t.genre);
        stmt.bindInt(14, t.year);
        stmt.bindInt(15, t.track_num);
        stmt.bindInt(16, t.disc_num);
        stmt.bindText(17, t.cover_art_path);
        stmt.bindInt(18, t.rating);
        stmt.bindInt(19, t.play_count);
        stmt.bindInt(20, t.last_played);
        if (t.date_added)
            stmt.bindInt(21, t.date_added);
        else
            stmt.bindNull(21);
        stmt.bindInt(22, t.last_scanned);
        stmt.bindInt(23, t.dirty ? 1 : 0);
        stmt.bindInt(24, t.library_id ? t.library_id : 1);
        if (t.deleted_at)
            stmt.bindInt(25, t.deleted_at);
        else
            stmt.bindNull(25);
        int rc = stmt.stepDone();
        if (rc == SQLITE_CONSTRAINT) {
            SqliteStatement sel;
            if (auto e = sel.prepare(h, "SELECT id FROM tracks WHERE fingerprint=?"); !e) {
                corrupt = true;
                break;
            }
            sel.bindBlob(1, fpSpan);
            if (sel.step()) {
                int64_t existing = sel.columnInt(0);
                SqliteStatement upd;
                if (auto e =
                        upd.prepare(h, "UPDATE tracks SET path=?, size=?, mtime=?, duration=?, "
                                       "sample_rate=?, channels=?, bitrate=?, title=?, artist=?, "
                                       "album=?, album_artist=?, genre=?, year=?, track_num=?, "
                                       "disc_num=?, cover_art_path=?, rating=?, play_count=?, "
                                       "last_played=?, date_added=?, last_scanned=?, dirty=?, "
                                       "library_id=?, deleted_at=? WHERE id=?");
                    !e) {
                    corrupt = true;
                    break;
                }
                upd.bindText(1, t.path);
                upd.bindInt(2, t.size);
                upd.bindInt(3, t.mtime);
                upd.bindDouble(4, t.duration);
                upd.bindInt(5, t.sample_rate);
                upd.bindInt(6, t.channels);
                upd.bindInt(7, t.bitrate);
                upd.bindText(8, t.title);
                upd.bindText(9, t.artist);
                upd.bindText(10, t.album);
                upd.bindText(11, t.album_artist);
                upd.bindText(12, t.genre);
                upd.bindInt(13, t.year);
                upd.bindInt(14, t.track_num);
                upd.bindInt(15, t.disc_num);
                upd.bindText(16, t.cover_art_path);
                upd.bindInt(17, t.rating);
                upd.bindInt(18, t.play_count);
                upd.bindInt(19, t.last_played);
                upd.bindInt(20, t.date_added);
                upd.bindInt(21, t.last_scanned);
                upd.bindInt(22, t.dirty ? 1 : 0);
                upd.bindInt(23, t.library_id ? t.library_id : 1);
                if (t.deleted_at)
                    upd.bindInt(24, t.deleted_at);
                else
                    upd.bindNull(24);
                upd.bindInt(25, existing);
                (void)upd.stepDone();
            } else {
                SqliteStatement sel2;
                if (auto e = sel2.prepare(h, "SELECT id FROM tracks WHERE path=?"); e) {
                    sel2.bindText(1, t.path);
                    if (sel2.step()) {
                    }
                }
            }
        } else if (rc != SQLITE_DONE) {
            corrupt = true;
            break;
        }
    }
    if (corrupt) {
        (void)tx.rollback();
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Corrupt, "import corrupt")};
    }
    if (auto c = tx.commit(); !c)
        return std::unexpected{c.error()};
    return {};
}

} // namespace caudio::db
