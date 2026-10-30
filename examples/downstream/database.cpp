#include <caudio/db/database.hpp>
#include <caudio/utils/error.hpp>
#include <iostream>

int main() {
    auto result = caudio::db::Database::open(":memory:");
    if (!result) return 1;
    std::cout << "database opened\n";
}
