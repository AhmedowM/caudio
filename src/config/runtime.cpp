#include <caudio/config/runtime.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/result.hpp>
#include <filesystem>
#include <string>
#include <system_error>

namespace caudio::config {

bool RuntimePaths::empty() const noexcept {
    return database.empty() && configuration.empty() && logs.empty() && cache.empty() &&
           importedMedia.empty() && socket.empty();
}

caudio::utils::Expected<void> RuntimePaths::ensureDirectories() const {
    auto create = [](const std::filesystem::path& path) -> caudio::utils::Expected<void> {
        if (path.empty())
            return {};
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        if (ec) {
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::Io, "cannot create runtime directory '" +
                                                   path.generic_string() + "': " + ec.message())};
        }
        return {};
    };

    if (!database.empty()) {
        if (auto r = create(database.parent_path()); !r)
            return r;
    }
    if (!configuration.empty()) {
        if (auto r = create(configuration.parent_path()); !r)
            return r;
    }
    if (auto r = create(logs); !r)
        return r;
    if (auto r = create(cache); !r)
        return r;
    return create(importedMedia);
}

} // namespace caudio::config
