#include <chrono>
#include <expected>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

import caudio.player;
import caudio.utils;

using namespace caudio::player;
using namespace caudio::utils;

namespace caudio::examples {

class MiniPlayer {
  public:
    Expected<void> run(const std::string &path) {
        // Open file
        auto readerResult = FileReader::open(path);
        if (!readerResult)
            return std::unexpected(readerResult.error());
        auto reader = std::move(readerResult.value());

        // Create decoder
        auto decResult = DecoderRegistry::open(*reader);
        if (!decResult)
            return std::unexpected(decResult.error());
        auto decoder = std::move(decResult.value());

        std::cout << "Opened: " << path << "\n";
        std::cout << "Sample rate: " << decoder->sampleRate() << " Hz\n";
        std::cout << "Channels: " << decoder->channels() << "\n";
        std::cout << "Total frames: " << decoder->totalFrames() << "\n";
        std::cout << "Duration: "
                  << static_cast<double>(decoder->totalFrames()) / decoder->sampleRate()
                  << " seconds\n\n";

        // Create ring buffer for audio data — use sample capacity large enough for 96kHz stereo
        // (~0.7s) Ring is sample-based (channels=1) so capacity is in floats; 65536 floats = 32768
        // frames stereo ≈ 0.34s at 96k, 0.74s at 44.1k
        constexpr std::size_t kRingCapacity = 65536;
        SpscRing<float> ring(kRingCapacity);

        // Create audio output
        AudioOutput::Config cfg;
        cfg.sampleRate = decoder->sampleRate();
        cfg.channels = decoder->channels();
        cfg.ring = &ring;
        auto outResult = AudioOutput::create(cfg);
        if (!outResult)
            return std::unexpected(outResult.error());
        auto output = std::move(outResult.value());

        // Preroll: fill ring with ~100ms of audio before starting device to avoid initial underrun
        // beep
        constexpr std::size_t kBufferFrames = 1024;
        std::vector<float> buffer(kBufferFrames * decoder->channels());
        std::size_t totalFramesPlayed = 0;
        {
            std::size_t prerollFrames = decoder->sampleRate() / 10; // ~100ms
            std::size_t filled = 0;
            while (filled < prerollFrames) {
                std::size_t frames =
                    decoder->decode(std::span<float>(buffer.data(), buffer.size()));
                if (frames == 0)
                    break;
                std::size_t samples = frames * decoder->channels();
                ring.write(std::span<float>(buffer.data(), samples));
                filled += frames;
                totalFramesPlayed += frames;
                if (ring.availableWrite() < buffer.size())
                    break;
            }
        }

        // Start playback - begins audio device after preroll
        output->start();
        std::cout << "Playing...\n";
        bool decodingFinished = false;

        while (output->isPlaying() && !decodingFinished) {
            // Check available space in ring buffer
            std::size_t available = ring.availableWrite();
            if (available >= buffer.size()) {
                // Decode directly into ring buffer
                std::size_t frames =
                    decoder->decode(std::span<float>(buffer.data(), buffer.size()));
                if (frames == 0) {
                    decodingFinished = true;
                    break;
                }
                std::size_t samples = frames * decoder->channels();
                std::size_t written = ring.write(std::span<float>(buffer.data(), samples));
                totalFramesPlayed += frames;
                if (written < samples) {
                    std::cout << "\nWarning: ring buffer overflow, " << (samples - written)
                              << " samples lost\n";
                }
            } else {
                // Ring buffer full, wait a bit
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
            }

            // Progress
            if (totalFramesPlayed % (decoder->sampleRate() * 2) == 0 && totalFramesPlayed > 0) {
                double elapsed = static_cast<double>(totalFramesPlayed) / decoder->sampleRate();
                std::cout << "\rPlayed: " << elapsed << "s / "
                          << static_cast<double>(decoder->totalFrames()) / decoder->sampleRate()
                          << "s" << std::flush;
            }
        }

        // Wait for playback to finish draining
        while (output->isPlaying() && ring.availableRead() > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }

        output->stop();
        std::cout << "\nDecoded " << totalFramesPlayed << " frames\n";
        std::cout << "Duration: " << static_cast<double>(totalFramesPlayed) / decoder->sampleRate()
                  << "s\n";

        return {};
    }
}; // class MiniPlayer

} // namespace caudio::examples

int main(int argc, char **argv) {
    using namespace caudio::examples;

    bool verifyOnly = false;
    std::string path;
    if (argc > 1) {
        path = argv[1];
        if (argc > 2 && std::string(argv[2]) == "--verify")
            verifyOnly = true;
        if (path == "--verify" && argc > 2) {
            path = argv[2];
            verifyOnly = true;
        }
    } else {
        path = std::filesystem::current_path().string() + "/tests/fixtures/sample.wav";
        std::string musicPath = "C:/Users/Secondary/Music/ALL ABOUT MY CLIQUE - TOPDOGFIGHT.mp3";
        if (std::filesystem::exists(musicPath)) {
            std::cout << "Found MP3 in Music folder, using it instead\n";
            path = musicPath;
        }
    }
    // If first arg is --verify without path, handle
    if (path == "--verify") {
        verifyOnly = true;
        if (argc > 2)
            path = argv[2];
        else
            path = std::filesystem::current_path().string() + "/tests/fixtures/sample.wav";
    }

    if (!std::filesystem::exists(path)) {
        std::cerr << "File not found: " << path << "\n";
        return 1;
    }

    if (verifyOnly) {
        // Decode-only verification path — no audio device, just counts frames
        auto readerResult = FileReader::open(path);
        if (!readerResult) {
            std::cerr << "Open failed: " << readerResult.error().message << "\n";
            return 1;
        }
        auto reader = std::move(readerResult.value());
        auto decResult = DecoderRegistry::open(*reader);
        if (!decResult) {
            std::cerr << "Decoder open failed: " << decResult.error().message << "\n";
            return 1;
        }
        auto decoder = std::move(decResult.value());
        std::cout << "Opened: " << path << "\n";
        std::cout << "Sample rate: " << decoder->sampleRate() << " Hz\n";
        std::cout << "Channels: " << decoder->channels() << "\n";
        std::cout << "Total frames: " << decoder->totalFrames() << "\n";
        std::cout << "Duration: "
                  << static_cast<double>(decoder->totalFrames()) / decoder->sampleRate()
                  << " seconds\n";
        constexpr std::size_t kBufFrames = 2048;
        std::vector<float> buf(kBufFrames * decoder->channels());
        std::size_t total = 0;
        std::size_t calls = 0;
        while (true) {
            std::size_t got = decoder->decode(std::span<float>(buf.data(), buf.size()));
            if (got == 0)
                break;
            total += got;
            ++calls;
            // Detect restart: track if we ever got huge jump? Just count calls
        }
        std::cout << "Decoded " << total << " frames in " << calls << " calls\n";
        std::cout << "Duration: " << static_cast<double>(total) / decoder->sampleRate() << "s\n";
        // Also test seek
        if (decoder->seek(0.0).has_value()) {
            std::size_t got2 = decoder->decode(std::span<float>(buf.data(), buf.size()));
            std::cout << "After seek(0) decode: " << got2 << " frames (should be >0)\n";
        }
        return 0;
    }

    MiniPlayer mini;
    auto result = mini.run(path);
    if (!result) {
        std::cerr << "Error: " << result.error().message
                  << " (code: " << static_cast<int>(result.error().code) << ")\n";
        return 1;
    }
    return 0;
}