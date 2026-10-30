#include <caudio/media/scan.hpp>
#include <caudio/utils/error.hpp>
#include <iostream>

int main() {
    auto result = caudio::media::scanTracksToVector(".");
    if (!result) return 1;
    std::cout << "discovered " << result->size() << " tracks\n";
}
