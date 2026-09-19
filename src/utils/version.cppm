module;
#include "caudio/utils/version.hpp"

export module caudio.utils:version;

export namespace caudio::utils {
  using ::caudio::utils::version;
  using ::caudio::utils::versionString;
  using ::caudio::utils::shortVersion;
  using ::caudio::utils::versionCommit;
  using ::caudio::utils::versionMajor;
  using ::caudio::utils::versionMinor;
  using ::caudio::utils::versionPatch;
  using ::caudio::utils::kVersion;
  using ::caudio::utils::kVersionFull;
  using ::caudio::utils::kVersionCommit;
}

export namespace caudio {
  using ::caudio::kVersion;
  using ::caudio::kVersionFull;
  using ::caudio::kVersionCommit;
}
