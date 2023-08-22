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

void SocketTraffic::SendTraffic(const std::string& connection_info,
                                const google::protobuf::Message& msg) {
  Socket socket;
  struct sockaddr_in address;
  struct in_addr host;

  struct sockaddr_in conn_address;
  struct in_addr conn_host;

  if (connections_.find(connection_info) == connections_.end()) {
    // split connection_info by chacater #
    size_t pos = connection_info.find("#");
    if (pos == std::string::npos) {
      Log("SocketTraffic::SendTraffic",
          "Invalid connection_info " + connection_info);
      return;
    }
    std::string client = connection_info.substr(0, pos);
    std::string server = connection_info.substr(pos + 1);
    std::string client_host = client.substr(0, client.find(":"));
    std::string client_port = client.substr(client.find(":") + 1);
    std::string server_host = server.substr(0, server.find(":"));
    std::string server_port = server.substr(server.find(":") + 1);
    Log("hfh", "Serveris " + server);

    if (!socket.Create()) {
      Log("SocketTraffic", "Failed to create socket ");
      return;
    }

    if (inet_pton(AF_INET, client_host.c_str(), &host) != 1) {
      throw new std::runtime_error("Invalid client host string " + client);
    }
    address.sin_family = AF_INET;
    address.sin_addr = host;
    address.sin_port = htons(stoi(client_port));
    Log("send:traffic::", "Binding to port " + client_port);
    if (!socket.Bind(address)) {
      std::runtime_error("Failed to bind to port " + client_port);
    }

    if (inet_pton(AF_INET, server_host.c_str(), &conn_host) != 1) {
      throw new std::runtime_error("Invalid client host string " + server);
    }
    conn_address.sin_family = AF_INET;
    conn_address.sin_addr = conn_host;
    conn_address.sin_port = htons(stoi(server_port));
    Log("MockServer", "Binding to port " + server_port);
    if (!socket.Connect(conn_address)) {
      std::runtime_error("Failed to connect to port " + server_port);
    }

    Log("SocketTraffic::SendTraffic", "Trying to send" + connection_info);
    std::string trace_pfix =
        "socket_rec_connect_from_" + client + "_from_" + server;
    if (!std::ifstream(trace_pfix + ".bin")) {
      throw new std::runtime_error("SendTraffic::send: trace with prefix " +
                                   trace_pfix + " does NOT exist");
    }
    airreplay::Trace trace(trace_pfix, airreplay::Mode::kReplay, false);
    trace.Coalesce();
    airreplay::TraceGroup trace_group({trace.traceEvents_});
    conn_threads_.emplace_back(std::thread([this, new_socket = std::move(socket),
                                traces = std::move(trace_group)]() mutable {
      Log("SocketTraffic::SendTraffic", "Sending traffic from new thread ");
      ReplayTrace(std::move(new_socket), traces, Log, "SocketTraffic::SendTraffic");
    }));

    connections_.emplace(connection_info, std::move(socket));
  }
}

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

      client_threads_.push_back(
          std::thread([this, new_socket = std::move(new_socket),
                       traces = traces_, port]() mutable {
            ReplayTrace(std::move(new_socket), traces, Log, "MockServer");
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

void ReplayTrace(
    Socket new_socket, airreplay::TraceGroup traces,
    std::function<void(const std::string&, const std::string&)> Log, std::string log_prefix) {
  while (true) {
    uint8_t buffer[8192] = {0};
    while (traces.NextIsReadOrEmpty()) {
      int length = new_socket.Read(buffer, sizeof(buffer));
      traces.ConsumeRead(buffer, length);
      Log(log_prefix, "Read " + std::to_string(length) + " bytes");
    }
    while (traces.NextIsWrite()) {
      int trace_length = traces.NextCommonWrite(buffer, sizeof(buffer));
      Log(log_prefix,
          "should write " + std::to_string(trace_length) + " bytes");
      int written_length = new_socket.Write(buffer, trace_length);
      Log(log_prefix, "wrote " + std::to_string(written_length) + " bytes");
      DCHECK(trace_length == written_length);
    }

    if (traces.AllEmpty()) {
      Log(log_prefix,
          "All traces are empty, socket replay done!Sleeping.....");
      std::this_thread::sleep_for(std::chrono::seconds(50));
      Log(log_prefix, "Sleeping is over. closing socket");
      new_socket.Close();
      return;
    }

    if (!(traces.NextIsReadOrEmpty() || traces.NextIsWrite())) {
      Log(log_prefix,
          "The set of traces associated with socket cannot agree on the "
          "next action");
      throw new std::runtime_error(
          "The set of traces associated with socket cannot agree on the "
          "next action");
    }
  }
}