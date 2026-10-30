#include <caudio/utils/error.hpp>
#include <caudio/utils/ring.hpp>
#include <iostream>

int main() {
    caudio::utils::SpscRing<float> ring(8, 2);
    std::cout << "capacity=" << ring.capacity() << '\n';
}
