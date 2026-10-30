#include <caudio/types/db.hpp>
#include <caudio/types/engine.hpp>
#include <iostream>

int main() {
    caudio::types::Track track;
    track.title = "An in-memory track";
    track.artist = "caudio";
    caudio::types::EngineConfig config;
    config.pollMs = 25;
    std::cout << track.title << " (" << config.pollMs << " ms)\n";
}
