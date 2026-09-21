module;
#include "caudio/utils/version.hpp"

export module caudio.utils:version;

export namespace caudio::utils {
using ::caudio::utils::kVersion;
using ::caudio::utils::kVersionCommit;
using ::caudio::utils::kVersionFull;
using ::caudio::utils::shortVersion;
using ::caudio::utils::version;
using ::caudio::utils::versionCommit;
using ::caudio::utils::versionMajor;
using ::caudio::utils::versionMinor;
using ::caudio::utils::versionPatch;
using ::caudio::utils::versionString;
} // namespace caudio::utils

export namespace caudio {
using ::caudio::kVersion;
using ::caudio::kVersionCommit;
using ::caudio::kVersionFull;
} // namespace caudio
