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

    // Create ring buffer for audio data
    constexpr std::size_t kRingCapacity = 8192;
    SpscRing<float> ring(kRingCapacity);

    // Create audio output
    AudioOutput::Config cfg;
    cfg.sampleRate = decoder->sampleRate();
    cfg.channels = decoder->channels();
    cfg.ring = &ring;
    auto outResult = AudioOutput::create(cfg);
    if (!outResult) return std::unexpected(outResult.error());
    auto output = std::move(outResult.value());

    // Start playback - begins audio device
    output->start();
    std::cout << "Playing...\n";

    // Decode loop - feed ring buffer
    constexpr std::size_t kBufferFrames = 1024;
    std::vector<float> buffer(kBufferFrames * decoder->channels());

    std::size_t totalFramesPlayed = 0;
    bool decodingFinished = false;

    while (output->isPlaying() && !decodingFinished) {
      // Check available space in ring buffer
      std::size_t available = ring.availableWrite();
      if (available >= buffer.size()) {
        // Decode directly into ring buffer
        std::size_t frames = decoder->decode(std::span<float>(buffer.data(), buffer.size()));
        if (frames == 0) {
          decodingFinished = true;
          break;
        }
        std::size_t samples = frames * decoder->channels();
        std::size_t written = ring.write(std::span<float>(buffer.data(), samples));
        totalFramesPlayed += frames;
        if (written < samples) {
          std::cout << "\nWarning: ring buffer overflow, " << (samples - written) << " samples lost\n";
        }
      } else {
        // Ring buffer full, wait a bit
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
      }

      // Progress
      if (totalFramesPlayed % (decoder->sampleRate() * 2) == 0 && totalFramesPlayed > 0) {
        double elapsed = static_cast<double>(totalFramesPlayed) / decoder->sampleRate();
        std::cout << "\rPlayed: " << elapsed << "s / "
                  << static_cast<double>(decoder->totalFrames()) / decoder->sampleRate() << "s" << std::flush;
      }
    }

    // Wait for playback to finish draining
    while (output->isPlaying() && ring.availableRead() > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }

    output->stop();
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