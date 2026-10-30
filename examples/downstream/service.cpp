#include <caudio/service/service.hpp>
#include <iostream>

int main() {
    caudio::service::ServiceConfig config;
    std::cout << "service configured with log level " << config.logLevel << '\n';
}
