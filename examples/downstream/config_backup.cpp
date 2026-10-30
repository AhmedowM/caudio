#include <caudio/config.hpp>
#include <caudio/utils/error.hpp>
#include <iostream>

int main() {
    caudio::config::Config config;
    config.device = "auto";
    config.logLevel = 3;
    if (auto result = caudio::config::saveConfig(config); !result) return 1;
    std::cout << "configuration saved\n";
}
