#include <caudio/db/database.hpp>
#include <caudio/engine/engine.hpp>
#include <caudio/types/engine.hpp>
#include <caudio/utils/error.hpp>
#include <iostream>
#include <memory>

int main() {
    auto database = caudio::db::Database::open(":memory:");
    if (!database) return 1;
    caudio::types::EngineConfig engineConfig;
    engineConfig.pollMs = 25;
    std::cout << "database plus engine configuration ready: "
              << engineConfig.pollMs << " ms\n";
}
