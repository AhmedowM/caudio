#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <iostream>

int main() {
    caudio::ipc::Command command{caudio::ipc::StatusReq{}};
    std::cout << "command variant index=" << command.index() << '\n';
}
