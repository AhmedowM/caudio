module;
#include <caudio/version.hpp>

export module caudio.version;

export namespace caudio::version {
using ::caudio::version::shortVersion;
using ::caudio::version::version;
using ::caudio::version::versionCommit;
using ::caudio::version::versionMajor;
using ::caudio::version::versionMinor;
using ::caudio::version::versionPatch;
using ::caudio::version::versionString;
} // namespace caudio::version

export namespace caudio::detail {
using ::caudio::detail::shortVersion;
using ::caudio::detail::versionCommit;
using ::caudio::detail::versionFull;
using ::caudio::detail::versionMajor;
using ::caudio::detail::versionMinor;
using ::caudio::detail::versionPatch;
} // namespace caudio::detail
