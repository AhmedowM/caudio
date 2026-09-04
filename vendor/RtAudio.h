#pragma once
// Minimal RtAudio stub for scaffold — replaces miniaudio vendor/miniaudio.h src/player/ca_output.c:27
// Real RtAudio will be vendored in Task 5.

#include <string>

class RtAudio {
public:
  enum Api { UNSPECIFIED, LINUX_ALSA, LINUX_PULSE, LINUX_OSS, UNIX_JACK, MACOSX_CORE, WINDOWS_WSAPI, WINDOWS_ASIO, WINDOWS_DS, RTAUDIO_DUMMY };
  enum RtAudioStreamStatus { RTAUDIO_INPUT_OVERFLOW = 0x1, RTAUDIO_OUTPUT_UNDERFLOW = 0x2 };
  struct StreamParameters {
    unsigned int deviceId = 0;
    unsigned int nChannels = 2;
    unsigned int firstChannel = 0;
  };
  struct StreamOptions {
    unsigned int flags = 0;
    unsigned int numberOfBuffers = 0;
    int priority = 0;
    std::string streamName;
  };
  using RtAudioCallback = int (*)(void *outputBuffer, void *inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status, void *userData);

  RtAudio(Api api = UNSPECIFIED) {}
  ~RtAudio() {}
  unsigned int getDeviceCount() { return 0; }
  bool isStreamOpen() const { return false; }
  bool isStreamRunning() const { return false; }
  void openStream(StreamParameters *outputParams, StreamParameters *inputParams, unsigned int format, unsigned int sampleRate, unsigned int *bufferFrames, RtAudioCallback callback, void *userData, StreamOptions *options = nullptr) {}
  void closeStream() {}
  void startStream() {}
  void stopStream() {}
  void abortStream() {}
};
