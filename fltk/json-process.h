// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "appearance.h"
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <thread>
class JsonProcess {
  pid_t pid = -1;
  int fd = -1, errors = -1;
  std::string incoming, outgoing, errorBuffer;

public:
  std::function<void(const Json &)> response;
  std::function<void(const std::string &)> failed, diagnostic;
  bool running() const { return pid > 0; }
  void start(
      const std::vector<std::string> &args,
      const std::vector<std::pair<std::string, std::string>> &overrides = {}) {
    stop();
    int pair[2], err[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair))
      throw std::runtime_error("Cannot create worker channel");
    if (pipe2(err, O_CLOEXEC)) {
      close(pair[0]);
      close(pair[1]);
      throw std::runtime_error("Cannot create worker diagnostics");
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pair[1], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pair[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, err[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, pair[0]);
    posix_spawn_file_actions_addclose(&actions, err[0]);
    std::vector<char *> argv;
    for (auto &s : args)
      argv.push_back(const_cast<char *>(s.c_str()));
    argv.push_back(nullptr);
    std::vector<std::string> environment;
    for (char **item = environ; *item; ++item) {
      std::string entry(*item);
      bool replaced = false;
      for (const auto &value : overrides)
        if (entry.compare(0, value.first.size() + 1, value.first + "=") == 0)
          replaced = true;
      if (!replaced)
        environment.push_back(std::move(entry));
    }
    for (const auto &value : overrides)
      environment.push_back(value.first + "=" + value.second);
    std::vector<char *> envp;
    for (auto &entry : environment)
      envp.push_back(entry.data());
    envp.push_back(nullptr);
    int error = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(),
                             envp.data());
    posix_spawn_file_actions_destroy(&actions);
    close(pair[1]);
    close(err[1]);
    if (error) {
      close(pair[0]);
      close(err[0]);
      pid = -1;
      throw std::runtime_error("Cannot start worker: " +
                               std::string(strerror(error)));
    }
    fd = pair[0];
    errors = err[0];
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    fcntl(errors, F_SETFL, fcntl(errors, F_GETFL) | O_NONBLOCK);
    incoming.clear();
    outgoing.clear();
    errorBuffer.clear();
  }
  void send(const Json &value) {
    if (!running())
      throw std::runtime_error("Worker is not running");
    outgoing += value.dump() + "\n";
    if (outgoing.size() > 1024 * 1024)
      throw std::runtime_error("Worker request queue exceeded limit");
  }
  void pump() {
    if (!running())
      return;
    while (!outgoing.empty()) {
      auto n = ::send(fd, outgoing.data(), outgoing.size(), MSG_NOSIGNAL);
      if (n > 0)
        outgoing.erase(0, n);
      else {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          fail("Worker disconnected");
          return;
        }
        break;
      }
    }
    char data[8192];
    for (;;) {
      auto n = read(fd, data, sizeof(data));
      if (n <= 0)
        break;
      incoming.append(data, n);
      if (incoming.size() > 4 * 1024 * 1024) {
        fail("Worker response exceeded limit");
        return;
      }
      while (incoming.find('\n') != std::string::npos) {
        auto pos = incoming.find('\n');
        auto line = incoming.substr(0, pos);
        incoming.erase(0, pos + 1);
        try {
          auto object = Json::parse(line);
          if (!object.is_object())
            throw std::runtime_error("Expected a JSON object");
          if (response)
            response(object);
        } catch (const std::exception &e) {
          if (failed)
            failed(std::string("Invalid worker response: ") + e.what());
        }
        if (!running())
          return;
      }
    }
    for (;;) {
      auto n = read(errors, data, sizeof(data));
      if (n <= 0)
        break;
      errorBuffer.append(data, n);
      if (errorBuffer.size() > 16384)
        errorBuffer.erase(0, errorBuffer.size() - 16384);
      if (diagnostic)
        diagnostic(std::string(data, n));
    }
    int status = 0;
    if (waitpid(pid, &status, WNOHANG) == pid) {
      pid = -1;
      close(fd);
      close(errors);
      fd = errors = -1;
      if (failed)
        failed("Worker exited" +
               (errorBuffer.empty()
                    ? ""
                    : ": " + errorBuffer.substr(errorBuffer.size() > 500
                                                    ? errorBuffer.size() - 500
                                                    : 0)));
    }
  }
  void fail(std::string reason) {
    stop();
    if (failed)
      failed(reason);
  }
  void stop() {
    if (pid > 0) {
      auto text = std::string("{\"action\":\"quit\"}\n");
      ::send(fd, text.data(), text.size(), MSG_NOSIGNAL);
      shutdown(fd, SHUT_WR);
      auto deadline =
          std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
      int status;
      while (waitpid(pid, &status, WNOHANG) == 0 &&
             std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      if (waitpid(pid, &status, WNOHANG) == 0) {
        kill(pid, SIGTERM);
        deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
        while (waitpid(pid, &status, WNOHANG) == 0 &&
               std::chrono::steady_clock::now() < deadline)
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (waitpid(pid, &status, WNOHANG) == 0) {
          kill(pid, SIGKILL);
          waitpid(pid, &status, 0);
        }
      }
      pid = -1;
    }
    if (fd >= 0)
      close(fd);
    if (errors >= 0)
      close(errors);
    fd = errors = -1;
    incoming.clear();
    outgoing.clear();
  }
  ~JsonProcess() { stop(); }
};
