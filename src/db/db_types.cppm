module;
#include "caudio/db/db_types.hpp"

export module caudio.db:types;

export namespace caudio::db {
  using ::caudio::db::Track;
  using ::caudio::db::Playlist;
  using ::caudio::db::QueueItem;
  using ::caudio::db::Queue;
  using ::caudio::db::HistoryEntry;
  using ::caudio::db::Bookmark;
  using ::caudio::db::Library;
  using ::caudio::db::DbStats;
  using ::caudio::db::LibraryStatsDetailedData;
  using ::caudio::db::TrackQuery;
  using ::caudio::db::HistoryQuery;
}
