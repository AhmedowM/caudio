#include "service_audio.hpp"

#include <caudio/media/decoder.hpp>
#include <caudio/media/reader.hpp>
#include <caudio/utils/error.hpp>
#include <cstdint>
#include <filesystem>

namespace caudio::service::detail {

double durationFromDecoder(const std::filesystem::path& path) noexcept {
    try {
        auto readerRes = caudio::media::FileReader::open(path);
        if (!readerRes)
            return 0.0;
        auto& readerPtr = readerRes.value();
        auto decRes = caudio::media::Decoder::open(*readerPtr);
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
