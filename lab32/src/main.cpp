#include <iostream>
#include "proxy/Proxy.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <PORT>" << std::endl;
        return 1;
    }

    int port = std::atoi(argv[1]);

    std::cout << "Hello from proxy!" << std::endl << "Starting at " << port << std::endl;

    proxy::Proxy proxy(port);

    proxy.run();

    return 0;
}
