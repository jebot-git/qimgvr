// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstring>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
// Owner-controlled local input channel. No external runtime or second XR
// session is involved; packets stay inside the owning viewer's private
// temporary folder.
class StereoControls {
  int fd = -1;
  std::filesystem::path path;

public:
  explicit StereoControls(const std::filesystem::path &directory)
      : path(directory / "controls.sock") {
    fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
      throw std::runtime_error("Cannot create stereo controls");
    int enabled = 1;
    setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled));
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.string().size() >= sizeof(address.sun_path)) {
      close(fd);
      fd = -1;
      throw std::runtime_error("Stereo control path is too long");
    }
    strcpy(address.sun_path, path.c_str());
    if (bind(fd, (sockaddr *)&address, sizeof(address))) {
      close(fd);
      fd = -1;
      throw std::runtime_error("Cannot bind stereo controls");
    }
    chmod(path.c_str(), 0600);
  }
  StereoControls(const StereoControls &) = delete;
  StereoControls &operator=(const StereoControls &) = delete;
  ~StereoControls() {
    if (fd >= 0)
      close(fd);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  }
  std::string endpoint() const { return path.string(); }
  void poll(const std::function<void(const std::string &)> &command) {
    for (int count = 0; count < 16; ++count) {
      char data[256];
      alignas(cmsghdr) char ancillary[CMSG_SPACE(sizeof(ucred))]{};
      iovec buffer{data, sizeof(data)};
      msghdr message{};
      message.msg_iov = &buffer;
      message.msg_iovlen = 1;
      message.msg_control = ancillary;
      message.msg_controllen = sizeof(ancillary);
      auto n = recvmsg(fd, &message, MSG_DONTWAIT);
      if (n < 0)
        break;
      if (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC))
        continue;
      bool own = false;
      for (auto c = CMSG_FIRSTHDR(&message); c; c = CMSG_NXTHDR(&message, c))
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_CREDENTIALS &&
            c->cmsg_len >= CMSG_LEN(sizeof(ucred))) {
          ucred credentials{};
          memcpy(&credentials, CMSG_DATA(c), sizeof(credentials));
          own = credentials.uid == getuid();
        }
      if (!own)
        continue;
      std::string value(data, n);
      if (command && (value == "Next" || value == "Previous" ||
                      value == "Close stereo view"))
        command(value);
    }
  }
  static bool send(const std::string &endpoint, const std::string &command) {
    if (endpoint.empty() || command.size() > 128)
      return false;
    int socketfd =
        socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (socketfd < 0)
      return false;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    bool ok = false;
    if (endpoint.size() < sizeof(address.sun_path)) {
      strcpy(address.sun_path, endpoint.c_str());
      ok = sendto(socketfd, command.data(), command.size(), MSG_NOSIGNAL,
                  (sockaddr *)&address,
                  sizeof(address)) == (ssize_t)command.size();
    }
    close(socketfd);
    return ok;
  }
};
