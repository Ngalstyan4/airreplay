#include "mock_socket_traffic.h"

#include <assert.h>
#include <glog/logging.h>
#include <stdint.h>
#include <unistd.h>

#include <filesystem>

#include "airreplay.h"
#include "trace.h"

// create sockets and listen to all these ports
SocketTraffic::SocketTraffic(std::string hoststr, std::vector<int> ports)
    : log_file_(std::string("socket_traffic.log"), std::ios::out) {
  Log("SocketTraffic", "Reading all traces");
  // create empty trace groups
  for (int port : ports) {
    traceGroups_.emplace(port, airreplay::TraceGroup());
    // Parse traces and add them to relevant trace groups
    ParseTraces(port, "accept");
    // create mock-servers around the trace groups
    servers_.emplace(
        port, std::make_unique<MockServer>(hoststr, port, traceGroups_[port]));
    Log("SocketTraffic", "Created mock-server for port" + std::to_string(port));
  }
}

void SocketTraffic::SendTraffic(int port, const uint8_t* buffer, int length) {}

void SocketTraffic::ParseTraces(int serverPort, std::string filter) {
  // iterate over all files in the current directory

  for (const auto& entry : std::filesystem::directory_iterator(".")) {
    std::string filename = entry.path();
    if (filename.find("socket_rec_") == std::string::npos) continue;

    int suffix_loc = filename.find(".bin");
    if (suffix_loc == std::string::npos) continue;

    if (filename.find(std::to_string(serverPort)) == std::string::npos)
      continue;
    if (filename.find(filter) == std::string::npos) continue;

    std::string traceprefix = filename.substr(0, suffix_loc);
    airreplay::Trace trace(traceprefix, airreplay::Mode::kReplay);
    int orig_len = trace.traceEvents_.size();
    trace.Coalesce();
    Log("SocketTraffic", "Parsing trace " + filename + " which has " +
                             std::to_string(orig_len) + "-->" +
                             std::to_string(trace.size()) + " elements");

    traceGroups_[serverPort].AddTrace(std::move(trace.traceEvents_));
  }
}

MockServer::MockServer(std::string hoststr, int port,
                       airreplay::TraceGroup traces)
    : log_file_("MockServer", std::ios::out), port_(port), traces_(traces) {
  Socket socket;
  if (!socket.Create()) {
    Log("SocketTraffic", "Failed to create socket ");
    return;
  }
  struct sockaddr_in address;
  struct in_addr host;
  if (inet_pton(AF_INET, hoststr.c_str(), &host) != 1) {
    throw new std::runtime_error("Invalid host string " + hoststr);
  }
  address.sin_family = AF_INET;
  address.sin_addr = host;
  address.sin_port = htons(port);
  Log("MockServer", "Binding to port " + std::to_string(port));
  if (!socket.Bind(address)) {
    std::runtime_error("Failed to bind to port " + std::to_string(port));
  }
  if (!socket.Listen(5)) {
    std::runtime_error("Failed to listen on port " + std::to_string(port));
  }
  bool is_nonblock = true;
  socket.IsNonBlocking(&is_nonblock);
  assert(!is_nonblock);
  sockets_.emplace(port, std::move(socket));
  assert(socket.fd_ == -1);

  accept_thread_ = std::thread([this, port]() {
    while (true) {
      Socket new_socket;
      if (!sockets_[port].Accept(new_socket)) {
        Log("MockServer",
            "Failed to accept connection on port " + std::to_string(port));
        new_socket.Close();
        return;
      }
      Log("MockServer", "Accepted connection to fd" +
                            std::to_string(new_socket.fd_) + " on port " +
                            std::to_string(port));

      client_threads_.push_back(std::thread([this,
                                             new_socket = std::move(new_socket),
                                             traces = traces_, port]() mutable {
        while (true) {
          uint8_t buffer[8192] = {0};
          while (traces.NextIsReadOrEmpty()) {
            int length = new_socket.Read(buffer, sizeof(buffer));
            traces.ConsumeRead(buffer, length);
            Log("MockServer", "Read " + std::to_string(length) + " bytes");
          }
          while (traces.NextIsWrite()) {
            int trace_length = traces.NextCommonWrite(buffer, sizeof(buffer));
            Log("MockServer", "should write " + std::to_string(trace_length) + " bytes");
            int written_length = new_socket.Write(buffer, trace_length);
            Log("MockServer", "wrote " + std::to_string(written_length) + " bytes");
            DCHECK(trace_length == written_length);
          }

          if (traces.AllEmpty()) {
            Log("MockServer", "All traces are empty, socket replay done!Sleeping.....");
            std::this_thread::sleep_for(std::chrono::seconds(50));
            Log("MockServer", "Sleeping is over. closing socket");
            new_socket.Close();
            return;
          }

          if (!(traces.NextIsReadOrEmpty() || traces.NextIsWrite())) {
            Log("MockServer", "The set of traces associated with socket cannot agree on the next action");
            throw new std::runtime_error(
                "The set of traces associated with socket cannot agree on the "
                "next action");
          }
        }
      }));
    }
  });
  Log("MockServer", "Started listening on port " + std::to_string(port));
}

MockServer::~MockServer() {
  // Log("MockServer", "Shutting down");
  // ^^ if this object was moved out of, Log will no logner be valid
  for (auto& conn_thread : client_threads_) {
    conn_thread.join();
  }
  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }
}