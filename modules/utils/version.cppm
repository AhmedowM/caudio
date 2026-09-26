module;
#include <caudio/version.hpp>

export module caudio.utils:version;

export namespace caudio::utils {
using ::caudio::utils::shortVersion;
using ::caudio::utils::version;
using ::caudio::utils::versionCommit;
using ::caudio::utils::versionMajor;
using ::caudio::utils::versionMinor;
using ::caudio::utils::versionPatch;
using ::caudio::utils::versionString;
} // namespace caudio::utils

export namespace caudio {
using ::caudio::shortVersion;
using ::caudio::versionCommit;
using ::caudio::versionFull;
using ::caudio::versionMajor;
using ::caudio::versionMinor;
using ::caudio::versionPatch;
} // namespace caudio
