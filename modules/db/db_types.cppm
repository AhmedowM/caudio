module;
#include <caudio/db/db_types.hpp>

export module caudio.db:types;

export namespace caudio::db {
using ::caudio::db::Bookmark;
using ::caudio::db::DbStats;
using ::caudio::db::HistoryEntry;
using ::caudio::db::HistoryQuery;
using ::caudio::db::Library;
using ::caudio::db::LibraryStatsDetailedData;
using ::caudio::db::Playlist;
using ::caudio::db::Queue;
using ::caudio::db::QueueItem;
using ::caudio::db::Track;
using ::caudio::db::TrackQuery;
} // namespace caudio::db
