#include "mock_socket_traffic.h"

#include <assert.h>
#include <stdint.h>
#include <unistd.h>

#include <iostream>

// create sockets and listen to all these ports
SocketTraffic::SocketTraffic(std::vector<int> ports) {
  for (int port : ports) {
    Socket socket;
    if (!socket.Create()) {
      Log("SocketTraffic", "Failed to create socket ");
      continue;
    }
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    Log("SocketTraffic", "Binding to port " + std::to_string(port));
    if (!socket.Bind(address)) {
      Log("SocketTraffic", "Failed to bind to port " + std::to_string(port));
      continue;
    }
    if (!socket.Listen(5)) {
      Log("SocketTraffic", "Failed to listen on port " + std::to_string(port));
      continue;
    }
    bool is_nonblock = true;
    socket.IsNonBlocking(&is_nonblock);
    assert(!is_nonblock);
    sockets_.emplace(port, std::move(socket));
    assert(socket.fd_ == -1);

    std::thread conn_thread([this, port]() {
      while (true) {
        Socket new_socket;
        if (!sockets_[port].Accept(new_socket)) {
          Log("SocketTraffic",
              "Failed to accept connection on port " + std::to_string(port));
          new_socket.Close();
          return;
        }
        Log("SocketTraffic", "Accepted connection to fd" +
                                 std::to_string(new_socket.fd_) + " on port " +
                                 std::to_string(port));
        while (true) {
          uint8_t buffer[1024] = {0};
          int length = new_socket.Read(buffer, sizeof(buffer));
          if (length == 0) {
            Log("SocketTraffic",
                "Connection closed on port " + std::to_string(port));
            new_socket.Close();
            break;
          }
          Log("SocketTraffic", "Received " + std::to_string(length) +
                                   " bytes on port " + std::to_string(port));
        }
      }
    });
    conn_threads_[port] = std::move(conn_thread);
  }
}

SocketTraffic::~SocketTraffic() {
  Log("SocketTraffic", "Shutting down");
  for (auto& conn_thread : conn_threads_) {
    conn_thread.second.join();
  }
}

void SocketTraffic::SendTraffic(int port, const uint8_t* buffer, int length) {}