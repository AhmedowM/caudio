#include <caudio/player/player.hpp>
#include <caudio/utils/error.hpp>
#include <iostream>

int main() {
    auto result = caudio::player::Player::create();
    if (!result) return 1;
    std::cout << "player created\n";
}
