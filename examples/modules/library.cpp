#include <iostream>
#include <memory>
import caudio.db;
import caudio.engine;

int main() {
    auto database = caudio::db::Database::open(":memory:");
    auto engine = caudio::engine::Engine::create();
    if (!database || !engine) return 1;
    if (auto result = engine.value()->attachDatabase(
            std::shared_ptr<caudio::db::Database>(std::move(database.value())));
        !result) return 1;
    std::cout << "module consumer ready\n";
}
