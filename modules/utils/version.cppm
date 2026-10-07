module;
#include <caudio/version.hpp>

export module caudio.utils:version;

export namespace caudio::version {
using ::caudio::version::shortVersion;
using ::caudio::version::version;
using ::caudio::version::versionCommit;
using ::caudio::version::versionMajor;
using ::caudio::version::versionMinor;
using ::caudio::version::versionPatch;
using ::caudio::version::versionString;
} // namespace caudio::version

export namespace caudio {
using ::caudio::shortVersion;
using ::caudio::versionCommit;
using ::caudio::versionFull;
using ::caudio::versionMajor;
using ::caudio::versionMinor;
using ::caudio::versionPatch;
} // namespace caudio
