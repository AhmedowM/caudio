#include <caudio/client/client.hpp>
#include <iostream>

int main() {
    caudio::client::Client client("library.db");
    std::cout << "database=" << client.dbPath().string() << '\n';
}
