#include "service_audio.hpp"

#include <caudio/player.hpp>
#include <caudio/utils.hpp>
#include <cstdint>

namespace caudio::service::detail {

double durationFromDecoder(const std::filesystem::path& path) noexcept {
    try {
        auto readerRes = caudio::player::FileReader::open(path);
        if (!readerRes)
            return 0.0;
        auto& readerPtr = readerRes.value();
        auto decRes = caudio::player::DecoderRegistry::open(*readerPtr);
        if (!decRes)
            return 0.0;
        auto& decPtr = decRes.value();
        std::uint32_t sr = decPtr->sampleRate();
        std::uint64_t frames = decPtr->totalFrames();
        if (sr == 0)
            return 0.0;
        return static_cast<double>(frames) / static_cast<double>(sr);
    } catch (...) {
        return 0.0;
    }
}

} // namespace caudio::service::detail
