module;
#include <caudio/db/scan.hpp>

export module caudio.db:scan;

export namespace caudio::db {
using ::caudio::db::scan;
using ::caudio::db::scanDirectory;
using ::caudio::db::scanLibrary;
using ::caudio::db::ScanMode;
} // namespace caudio::db

export namespace caudio::db::detail {
using ::caudio::db::detail::hasAudioExt;
}
