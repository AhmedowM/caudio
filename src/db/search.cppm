module;
#include "caudio/db/search.hpp"

export module caudio.db:search;

export namespace caudio::db {
  using ::caudio::db::fillTrackSearch;
  using ::caudio::db::tryFtsQuery;
  using ::caudio::db::searchFts;
  using ::caudio::db::searchLike;
  using ::caudio::db::sanitizeFtsTerm;
  using ::caudio::db::search;
}
