#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <sys/socket.h>

// A blocking socket class that wrapps around the socket system calls
class Socket {
 public:
  Socket();

  Socket(Socket&& other) noexcept;
  Socket& operator=(Socket&& other) noexcept;
  Socket& operator=(const Socket&) = delete;
  Socket(const Socket&) = delete;
  ~Socket();

  bool Create();
  int Release();
  bool Bind(const sockaddr_in& address);
  bool Listen(int backlog);
  bool Accept(Socket& new_socket, sockaddr* remote = nullptr);
  bool Connect(const sockaddr_in& address);
  int Read(uint8_t* buffer, int length);
  int Write(const uint8_t* buffer, int length);
  bool Close();
  bool Reset(int fd);

  bool SetNonBlocking(bool enabled);
  bool IsNonBlocking(bool* is_non_blocking) const;
  bool SetTCPNoDelay(bool enabled);
  bool GetTCPNoDelay(int* enabled);
  bool GetSockOpt(int level, int option, int* value);
  bool SetSockOpt(int level, int option, int value);

  //  private:
  int fd_;
};