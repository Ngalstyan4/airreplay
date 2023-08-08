#pragma once

#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "socket.h"

class SocketTraffic {
 public:
  SocketTraffic(std::vector<int> ports);
  ~SocketTraffic();
  void SendTraffic(int port, const uint8_t* buffer, int length);

 private:
  using Logger = std::function<void(const std::string&, const std::string&)>;

  std::map<int, std::thread> conn_threads_;
  std::map<int, Socket> sockets_;

  std::mutex log_lock_;
  Logger Log = [this](const std::string& tag, const std::string& msg) {
    std::lock_guard<std::mutex> lock(log_lock_);
    std::cerr << tag << ": " << msg << std::endl;
  };
};