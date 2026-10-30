#include <caudio/engine/engine.hpp>
#include <caudio/types/engine.hpp>
#include <caudio/utils/error.hpp>
#include <iostream>

int main() {
    caudio::types::EngineConfig config;
    config.pollMs = 25;
    std::cout << "engine poll interval=" << config.pollMs << " ms\n";
}
