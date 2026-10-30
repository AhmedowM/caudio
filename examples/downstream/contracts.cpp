#include <caudio/contracts/database.hpp>
#include <type_traits>

static_assert(std::is_abstract_v<caudio::contracts::IDatabase>);

int main() {}
