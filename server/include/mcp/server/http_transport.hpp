#pragma once

#include "mcp/server/transport.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace httplib {
class Server;
}

namespace mcp::server {

struct HttpTransportOptions {
    std::string listen_host = "127.0.0.1";
    int listen_port = 0;
    std::string path = "/mcp";
};

class HttpTransport final : public Transport {
public:
    explicit HttpTransport(HttpTransportOptions options);
    ~HttpTransport() override;

    HttpTransport(const HttpTransport&) = delete;
    HttpTransport& operator=(const HttpTransport&) = delete;

    core::Result<core::Unit> start(RequestHandler handler) override;
    void stop() noexcept override;
    std::string_view name() const noexcept override;

private:
    HttpTransportOptions options_;
    std::unique_ptr<httplib::Server> server_;
};

} // namespace mcp::server
