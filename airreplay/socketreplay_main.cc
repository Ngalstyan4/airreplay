#include <iostream>

#include "mock_socket_traffic.h"

int main() { 
    SocketTraffic controller("10.0.0.0", {7000, 7001});
    
    // sleep forever 
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
 }