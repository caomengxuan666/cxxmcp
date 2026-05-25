// Copyright (c) 2025 [caomengxuan666]

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "cxxmcp/core/result.hpp"

namespace mcp::gateway {

class Runtime {
 public:
  class Builder {
   public:
    Builder();
    Builder(const Builder&) = delete;
    Builder& operator=(const Builder&) = delete;
    Builder(Builder&&) noexcept;
    Builder& operator=(Builder&&) noexcept;
    ~Builder();

    Builder& profile(std::string id);
    Builder& host(std::string host);
    Builder& port(std::uint16_t port);
    Builder& path(std::string path = "/mcp");
    Builder& instruction(std::string value);
    Builder& trust(bool enabled = true);
    Builder& discover(bool enabled = true);
    Builder& bind_server(std::string server_id);
    Builder& add_stdio_server(std::string id, std::string command,
                              std::vector<std::string> args = {});
    Builder& add_http_server(std::string id, std::string url);
    Builder& add_env(std::string server_id, std::string name,
                     std::string value);
    Builder& add_header(std::string server_id, std::string name,
                        std::string value);

    core::Result<Runtime> build() const;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;
  Runtime(Runtime&&) noexcept;
  Runtime& operator=(Runtime&&) noexcept;
  ~Runtime();

  static Builder builder();

  core::Result<core::Unit> start();
  core::Result<core::Unit> stop();
  int run();

 private:
  struct Impl;
  explicit Runtime(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;

  friend class Builder;
};

}  // namespace mcp::gateway
