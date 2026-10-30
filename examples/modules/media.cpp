#include <iostream>
import caudio.media;

int main() {
    auto tracks = caudio::media::scanTracksToVector(".");
    std::cout << (tracks ? tracks->size() : 0) << " tracks\n";
}
