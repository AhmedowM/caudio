#include <caudio/app/paths.hpp>
#include <iostream>
#include <vector>

int main() {
    std::vector<std::string> paths;
    std::vector<std::string> unmatched;
    caudio::app::expandAddToken(".", false, paths, unmatched);
    std::cout << "expanded paths=" << paths.size() << '\n';
}
