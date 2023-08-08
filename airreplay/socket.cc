#include "socket.h"

#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <glog/logging.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <stdexcept>

Socket::Socket() : fd_(-1) {}

Socket::Socket(Socket&& other) noexcept : fd_(other.Release()) {}

Socket::~Socket() {
  DCHECK(fd_ == -1) << "Socket not closed before destruction";
}

// Create the underlying socket
bool Socket::Create() {
  fd_ = socket(AF_INET, SOCK_STREAM, 0);
  return fd_ != -1;
}

int Socket::Release() {
  int fd = fd_;
  fd_ = -1;
  return fd;
}

// Bind the socket to an address
bool Socket::Bind(const struct sockaddr_in& address) {
  if (fd_ == -1) return false;

  if (::bind(fd_, (const struct sockaddr*)&address, sizeof(address)) == -1) {
    std::cout << "Socket ERRNO" << std::to_string(errno);
    return false;
  }

  return true;
}

// Start listening for incoming connections and limit the backlog queue of
// incoming connections
bool Socket::Listen(int backlog) {
  if (fd_ == -1) return false;

  if (::listen(fd_, backlog) == -1) return false;

  return true;
}

bool Socket::Accept(Socket& new_socket, sockaddr* remote) {
  if (fd_ == -1) return false;

  socklen_t len = 0;
  if (remote) {
    len = sizeof(sockaddr_in);
  }
  int new_fd = ::accept(fd_, (sockaddr*)remote, &len);
  if (new_fd == -1) {
    std::cout << "Socket accept ERRNO" << std::to_string(errno);
    return false;
  }

  new_socket.Reset(new_fd);
  return true;
}

// Connect the socket to an address
bool Socket::Connect(const struct sockaddr_in& address) {
  DCHECK(fd_ != -1) << "Socket not created before connect";

  if (::connect(fd_, (const sockaddr*)&address, sizeof(address)) == -1)
    return false;

  return true;
}

// Read data from the socket
int Socket::Read(uint8_t* buffer, int length) {
  if (fd_ == -1) return false;

  int n = ::read(fd_, buffer, length);
  std::cerr << "Socket: Read " << std::to_string(n) << " bytes" << std::endl;
  return n;
}

// Write data to the socket
bool Socket::Write(const uint8_t* buffer, int length) {
  if (fd_ == -1) return false;

  int n = ::write(fd_, buffer, length);
  return n > 0;
}

bool Socket::Close() {
  if (fd_ == -1) return false;

  if (::close(fd_) == -1) return false;

  fd_ = -1;
  return true;
}

bool Socket::Reset(int fd) {
  if (fd != -1) Close();
  fd_ = fd;
  return true;
}

bool Socket::SetNonBlocking(bool enabled) {
  int curflags = ::fcntl(fd_, F_GETFL, 0);
  if (curflags == -1) {
    int err = errno;
    std::cerr << "Failed to get file status flags on fd " << std::to_string(fd_)
              << "error: " << std::to_string(err) << std::endl;
    return false;
  }

  int newflags = (enabled) ? (curflags | O_NONBLOCK) : (curflags & ~O_NONBLOCK);
  if (::fcntl(fd_, F_SETFL, newflags) == -1) {
    int err = errno;
    std::cerr << "Failed to set socket nonBlocking to "
              << std::to_string(enabled) << " on fd " << std::to_string(fd_)
              << "error: " << std::to_string(err) << std::endl;
  }
  return false;
}

bool Socket::IsNonBlocking(bool* is_nonblocking) const {
  int curflags = ::fcntl(fd_, F_GETFL, 0);
  if (curflags == -1) {
    int err = errno;
    std::cerr << "Failed to get file status flags on fd " << std::to_string(fd_)
              << "error: " << std::to_string(err) << std::endl;
    return false;
  }
  *is_nonblocking = ((curflags & O_NONBLOCK) != 0);
  return true;
}