module;
#include <caudio/db/scan.hpp>

export module caudio.db:scan;

// The db-side scan API is the persistence half only: `scanLibrary` upserts the
// tracks discovered by `caudio::media::scanTracks`.
export namespace caudio::db {
using ::caudio::db::scanLibrary;
} // namespace caudio::db

export namespace caudio::db::detail {
using ::caudio::media::hasAudioExt;
}
