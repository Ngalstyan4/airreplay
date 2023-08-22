#pragma once

#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "socket.h"
#include "trace.h"

void ReplayTrace(
    Socket new_socket, airreplay::TraceGroup traces,
    std::function<void(const std::string&, const std::string&)> Log =
        [](std::string, std::string) {}, std::string log_prefix = "");
class MockServer {
 public:
  MockServer(std::string hoststr, int port, airreplay::TraceGroup traces);
  MockServer(const MockServer&) = delete;
  MockServer& operator=(const MockServer&) = delete;
  // cannot move because it creates threads that reference self
  MockServer(MockServer&&) = delete;
  MockServer& operator=(MockServer&&) = delete;
  ~MockServer();
  void Start();
  void Stop();

 private:
  using Logger = std::function<void(const std::string&, const std::string&)>;
  std::ofstream log_file_;
  std::mutex log_lock_;
  int port_;
  std::thread accept_thread_;
  std::vector<std::thread> client_threads_;
  std::map<int, Socket> sockets_;
  // ground-truth traces for all sockets this server has to mock
  // the member is copied into each socket the MockServer manages and is
  // narrowed down to the correct socket trace, based on server-client
  // conversation
  const airreplay::TraceGroup traces_;
  bool running_ = false;

  Logger Log = [this](const std::string& tag, const std::string& msg) {
    std::lock_guard<std::mutex> lock(log_lock_);
    log_file_ << tag << ": " << msg << std::endl;
    log_file_.flush();
  };
};

class SocketTraffic {
 public:
  SocketTraffic(std::string host, std::vector<int> ports);
  // void SendTraffic(int port, const uint8_t* buffer, int length);
  void SendTraffic(const std::string& connection_info,
                   const google::protobuf::Message& msg);
  void ParseTraces(int serverPort, std::string filter);

 private:
  using Logger = std::function<void(const std::string&, const std::string&)>;
  using Port = int;
  using ConnectionInfo = std::string;

  // std::map<int, std::thread> conn_threads_;
  // std::map<int, Socket> sockets_;
  std::map<Port, airreplay::TraceGroup> traceGroups_;
  std::map<Port, std::unique_ptr<MockServer>> servers_;
  std::map<ConnectionInfo, Socket> connections_;
  std::vector<std::thread> conn_threads_;
  std::ofstream log_file_;

  std::mutex log_lock_;
  Logger Log = [this](const std::string& tag, const std::string& msg) {
    std::lock_guard<std::mutex> lock(log_lock_);
    log_file_ << tag << ": " << msg << std::endl;
    log_file_.flush();
  };
};
