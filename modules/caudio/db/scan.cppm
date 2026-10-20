module;
#include <caudio/db/scan.hpp>

export module caudio.db:scan;

// The db-side scan API is the persistence half only: `scanLibrary` upserts the
// tracks discovered by `caudio::media::scanTracks`. `ScanMode` and
// `scanDirectory` stay exported as compatibility forwarders to the media
// spellings (the module API must not change).
export namespace caudio::db {
using ::caudio::db::scanDirectory;
using ::caudio::db::scanLibrary;
using ::caudio::db::ScanMode;
} // namespace caudio::db

export namespace caudio::db::detail {
using ::caudio::db::detail::hasAudioExt;
}
