#include <caudio/player/player.hpp>
#include <caudio/utils/error.hpp>
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: mini_player FILE\n";
        return 2;
    }
    auto result = caudio::player::Player::create();
    if (!result || !result.value()->open(argv[1]) || !result.value()->play()) return 1;
    while (result.value()->state() == caudio::player::State::Playing)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
