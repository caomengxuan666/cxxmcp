#pragma once

#include "mcp/server/transport.hpp"

#include <iosfwd>
#include <string_view>

namespace mcp::server {

class StdioTransport final : public Transport {
public:
    StdioTransport();
    StdioTransport(std::istream& input, std::ostream& output);

    core::Result<core::Unit> start(RequestHandler handler) override;
    void stop() noexcept override;
    std::string_view name() const noexcept override;

private:
    std::istream* input_;
    std::ostream* output_;
    bool running_ = false;
};

} // namespace mcp::server
