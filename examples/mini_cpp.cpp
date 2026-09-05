#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <filesystem>
#include <vector>
#include <memory>
#include <expected>

import caudio.player;
import caudio.utils;

using namespace caudio::player;
using namespace caudio::utils;

namespace caudio::examples {

class MiniPlayer {
public:
  Expected<void> run(const std::string& path) {
    // Open file
    auto readerResult = FileReader::open(path);
    if (!readerResult) return std::unexpected(readerResult.error());
    auto reader = std::move(readerResult.value());

    // Create decoder
    auto decResult = DecoderRegistry::open(*reader);
    if (!decResult) return std::unexpected(decResult.error());
    auto decoder = std::move(decResult.value());

    std::cout << "Opened: " << path << "\n";
    std::cout << "Sample rate: " << decoder->sampleRate() << " Hz\n";
    std::cout << "Channels: " << decoder->channels() << "\n";
    std::cout << "Total frames: " << decoder->totalFrames() << "\n";
    std::cout << "Duration: " << static_cast<double>(decoder->totalFrames()) / decoder->sampleRate() << " seconds\n\n";

    // Create audio output
    AudioOutput::Config cfg;
    cfg.sampleRate = decoder->sampleRate();
    cfg.channels = decoder->channels();
    auto outResult = AudioOutput::create(cfg);
    if (!outResult) return std::unexpected(outResult.error());
    auto output = std::move(outResult.value());

    // Decode and play
    constexpr std::size_t kBufferFrames = 1024;
    std::vector<float> buffer(kBufferFrames * decoder->channels());

    std::size_t totalFramesPlayed = 0;
    auto startTime = std::chrono::steady_clock::now();

    while (true) {
      std::size_t frames = decoder->decode(std::span<float>(buffer.data(), buffer.size()));
      if (frames == 0) break;

      // Fill output ring buffer (simplified - just print for now)
      // In real implementation, this would feed to miniaudio device callback
      totalFramesPlayed += frames;

      // Simple progress
      if (totalFramesPlayed % (decoder->sampleRate() * 2) == 0) {
        double elapsed = static_cast<double>(totalFramesPlayed) / decoder->sampleRate();
        std::cout << "\rPlayed: " << elapsed << "s / "
                  << static_cast<double>(decoder->totalFrames()) / decoder->sampleRate() << "s" << std::flush;
      }
    }

    std::cout << "\nDecoded " << totalFramesPlayed << " frames\n";
    std::cout << "Duration: " << static_cast<double>(totalFramesPlayed) / decoder->sampleRate() << "s\n";

    return {};
  }
}; // class MiniPlayer

} // namespace caudio::examples

int main(int argc, char** argv) {
  using namespace caudio::examples;

  std::string path;
  if (argc > 1) {
    path = argv[1];
  } else {
    path = std::filesystem::current_path().string() + "/tests/fixtures/sample.wav";
    std::string musicPath = "C:/Users/Secondary/Music/ALL ABOUT MY CLIQUE - TOPDOGFIGHT.mp3";
    if (std::filesystem::exists(musicPath)) {
      std::cout << "Found MP3 in Music folder, using it instead\n";
      path = musicPath;
    }
  }

  if (!std::filesystem::exists(path)) {
    std::cerr << "File not found: " << path << "\n";
    return 1;
  }

  MiniPlayer mini;
  auto result = mini.run(path);
  if (!result) {
    std::cerr << "Error: " << result.error().message << " (code: " << static_cast<int>(result.error().code) << ")\n";
    return 1;
  }
  return 0;
}