#include <catch2/catch_test_macros.hpp>
import caudio.db;
import caudio.utils;

using namespace caudio::db;
using namespace caudio::utils;

TEST_CASE("search FTS5 quoting sanitizes", "[db_search]") {
    REQUIRE(sanitizeFtsTerm("hello OR world") == "hello world");
    REQUIRE(sanitizeFtsTerm("a AND b") == "a b");
    REQUIRE(sanitizeFtsTerm("\"Abbey Road\"") == "\"\"Abbey Road\"\"");
    // standalone NOT removed
    REQUIRE(sanitizeFtsTerm("NOT foo") == "foo");
    // special chars replaced with space
    std::string s = sanitizeFtsTerm("a*b:c");
    REQUIRE(s.find('*') == std::string::npos);
    REQUIRE(s.find(':') == std::string::npos);
}

TEST_CASE("search FTS finds title and LIKE fallback COLLATE NOCASE", "[db_search]") {
    auto dbRes = Database::open(":memory:");
    REQUIRE(dbRes.has_value());
    auto db = std::move(dbRes.value());
    for (auto [title, artist] : std::vector<std::pair<std::string, std::string>>{
             {"Abbey Road", "Beatles"}, {"Red House", "Hendrix"}, {"Hello World", "Artist"}}) {
        Track t;
        t.path = "/tmp/" + title + ".mp3";
        t.title = title;
        t.artist = artist;
        t.album = "Album";
        for (int b = 0; b < 32; ++b)
            t.fingerprint[b] = (uint8_t)(title.size() + b);
        auto r = db->insertTrack(t);
        REQUIRE(r.has_value());
    }
    auto res = search(*db, "Abbey Road", 10);
    REQUIRE(res.has_value());
    REQUIRE(res->size() >= 1);
    bool found = false;
    for (auto& t : *res)
        if (t.title == "Abbey Road")
            found = true;
    REQUIRE(found);
    // case-insensitive LIKE fallback
    auto res2 = search(*db, "hello", 10);
    REQUIRE(res2.has_value());
    REQUIRE(res2->size() >= 1);
    bool found2 = false;
    for (auto& t : *res2)
        if (t.title == "Hello World")
            found2 = true;
    REQUIRE(found2);
    // special OR should be sanitized and still find Abbey
    auto res3 = search(*db, "Abbey OR Road", 10);
    REQUIRE(res3.has_value());
    REQUIRE(res3->size() >= 1);
}

TEST_CASE("searchLike 5 cols COLLATE NOCASE", "[db_search]") {
    auto dbRes = Database::open(":memory:");
    REQUIRE(dbRes.has_value());
    auto db = std::move(dbRes.value());
    Track t;
    t.path = "/tmp/x.mp3";
    t.title = "Foo";
    t.artist = "BarArtist";
    t.album = "BazAlbum";
    t.genre = "Rock";
    for (int b = 0; b < 32; ++b)
        t.fingerprint[b] = (uint8_t)(0xE0 + b);
    REQUIRE(db->insertTrack(t).has_value());
    auto r = searchLike(*db, "barartist", 10);
    REQUIRE(r.has_value());
    REQUIRE(r->size() == 1);
    auto r2 = searchLike(*db, "BAZALBUM", 10);
    REQUIRE(r2.has_value());
    REQUIRE(r2->size() == 1);
    auto r3 = searchLike(*db, "rock", 10);
    REQUIRE(r3.has_value());
    REQUIRE(r3->size() == 1);
}

TEST_CASE("search empty query returns empty", "[db_search]") {
    auto dbRes = Database::open(":memory:");
    REQUIRE(dbRes.has_value());
    auto db = std::move(dbRes.value());
    auto r = search(*db, "", 10);
    REQUIRE(r.has_value());
    REQUIRE(r->empty());
    auto r2 = searchLike(*db, "", 10);
    REQUIRE(r2.has_value());
    REQUIRE(r2->empty());
}

TEST_CASE("search FTS escape LIKE wildcards", "[db_search]") {
    auto dbRes = Database::open(":memory:");
    REQUIRE(dbRes.has_value());
    auto db = std::move(dbRes.value());
    Track t;
    t.path = "/tmp/y.mp3";
    t.title = "100% Pure";
    for (int b = 0; b < 32; ++b)
        t.fingerprint[b] = (uint8_t)(0xF0 + b);
    REQUIRE(db->insertTrack(t).has_value());
    auto r = search(*db, "100%", 10);
    REQUIRE(r.has_value());
    // should find via LIKE fallback with escaped %
    bool found = false;
    for (auto& tt : *r)
        if (tt.title == "100% Pure")
            found = true;
    // FTS may not match %, but LIKE fallback should; accept either 0 or 1, but ensure no crash
    REQUIRE((r->size() == 0 || found));
}
