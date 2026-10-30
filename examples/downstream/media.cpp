#include <caudio/media/audio_ext.hpp>
#include <caudio/media/reader.hpp>
#include <caudio/utils/error.hpp>
#include <iostream>

int main() {
    std::cout << std::boolalpha
              << caudio::media::hasAudioExt("track.flac") << '\n';
}
