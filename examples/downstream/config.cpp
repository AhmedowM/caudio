#include <caudio/config.hpp>
#include <caudio/utils/error.hpp>
#include <iostream>

int main() {
    const auto result = caudio::config::socketPathFor("library.db");
    if (!result) return 1;
    std::cout << *result << '\n';
}
