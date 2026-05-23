#include "mcp/server/server.hpp"

#include "mcp/protocol/serialization.hpp"

#include <optional>
#include <utility>

namespace mcp::server {

namespace {

protocol::Json capability_to_json(const protocol::ServerCapabilities& capabilities) {
    protocol::Json json = protocol::Json::object();
    json["tools"] = {{"listChanged", capabilities.tools.list_changed}};
    json["resources"] = {{"listChanged", capabilities.resources.list_changed},
                          {"subscribe", capabilities.resources.subscribe}};
    json["prompts"] = {{"listChanged", capabilities.prompts.list_changed}};
    json["logging"] = {{"enabled", capabilities.logging.enabled}};
    return json;
}

protocol::Json server_info_to_json(const ServerOptions& options) {
    return protocol::Json{
        {"name", options.server_name},
        {"version", options.server_version},
    };
}

core::Result<protocol::JsonRpcResponse> make_error_response(const protocol::JsonRpcRequest& request,
                                                            int code,
                                                            std::string message,
                                                            std::string detail = {}) {
    return protocol::make_error_response(
        std::optional<protocol::RequestId>{request.id},
        protocol::make_error(code, std::move(message), detail.empty() ? std::nullopt : std::optional<protocol::Json>{detail}));
}

} // namespace

Server::Server(ServerOptions options)
    : options_(std::move(options)) {}

ToolRegistry& Server::tools() noexcept {
    return tools_;
}

const ToolRegistry& Server::tools() const noexcept {
    return tools_;
}

core::Result<protocol::JsonRpcResponse> Server::handle_request(const protocol::JsonRpcRequest& request,
                                                               const SessionContext& context) {
    if (auth_provider_) {
        AuthRequest auth_request;
        auth_request.remote_address = context.remote_address;
        const auto identity = auth_provider_->authenticate(auth_request);
        if (!identity) {
            return make_error_response(request,
                                       static_cast<int>(protocol::ErrorCode::PermissionDenied),
                                       "authentication failed",
                                       identity.error().message);
        }
    }

    if (rate_limiter_) {
        const auto decision = rate_limiter_->check(RateLimitRequest{
            .subject = context.session_id,
            .method = request.method,
            .request_bytes = request.params.dump().size(),
        });
        if (!decision) {
            return make_error_response(request,
                                       static_cast<int>(protocol::ErrorCode::RateLimited),
                                       "rate limiting failed",
                                       decision.error().message);
        }

        if (!decision->allowed) {
            return make_error_response(request,
                                       static_cast<int>(protocol::ErrorCode::RateLimited),
                                       "request rate limited");
        }
    }

    if (request.method == protocol::InitializeMethod) {
        protocol::Json result = protocol::Json::object();
        result["protocolVersion"] = std::string(protocol::McpProtocolVersion);
        result["capabilities"] = capability_to_json(options_.capabilities);
        result["serverInfo"] = server_info_to_json(options_);
        if (!options_.instructions.empty()) {
            result["instructions"] = options_.instructions;
        }
        return protocol::make_response(request.id, std::move(result));
    }

    if (request.method == protocol::PingMethod) {
        return protocol::make_response(request.id, protocol::Json::object());
    }

    if (request.method == "tools/list") {
        protocol::Json result = protocol::Json::object();
        result["tools"] = protocol::Json::array();
        for (const auto& tool : tools_.list()) {
            result["tools"].push_back(protocol::tool_definition_to_json(tool));
        }
        return protocol::make_response(request.id, std::move(result));
    }

    if (request.method == "tools/call") {
        if (!request.params.is_object()) {
            return make_error_response(request,
                                       static_cast<int>(protocol::ErrorCode::InvalidRequest),
                                       "tools/call params must be an object");
        }

        if (!request.params.contains("name") || !request.params.at("name").is_string()) {
            return make_error_response(request,
                                       static_cast<int>(protocol::ErrorCode::InvalidRequest),
                                       "tools/call requires a string name");
        }

        protocol::Json arguments = protocol::Json::object();
        if (request.params.contains("arguments")) {
            if (!request.params.at("arguments").is_object()) {
                return make_error_response(request,
                                           static_cast<int>(protocol::ErrorCode::InvalidRequest),
                                           "tools/call arguments must be an object");
            }
            arguments = request.params.at("arguments");
        }

        const auto result = tools_.call(request.params.at("name").get<std::string>(), std::move(arguments));
        if (!result) {
            return protocol::make_error_response(std::optional<protocol::RequestId>{request.id},
                                                 protocol::make_error(result.error().code,
                                                                      result.error().message,
                                                                      result.error().detail.empty()
                                                                          ? std::nullopt
                                                                          : std::optional<protocol::Json>{result.error().detail}));
        }

        return protocol::make_response(request.id, protocol::tool_result_to_json(*result));
    }

    return make_error_response(request,
                               static_cast<int>(protocol::ErrorCode::MethodNotFound),
                               "method not found",
                               request.method);
}

core::Result<core::Unit> Server::add_transport(std::unique_ptr<Transport> transport) {
    if (!transport) {
        return std::unexpected(core::Error{
            static_cast<int>(protocol::ErrorCode::InvalidRequest),
            "transport must not be null",
            {},
        });
    }

    transports_.push_back(std::move(transport));
    return core::Unit{};
}

void Server::set_auth_provider(std::unique_ptr<AuthProvider> auth_provider) {
    auth_provider_ = std::move(auth_provider);
}

void Server::set_rate_limiter(std::unique_ptr<RateLimiter> rate_limiter) {
    rate_limiter_ = std::move(rate_limiter);
}

core::Result<core::Unit> Server::start() {
    for (auto& transport : transports_) {
        const auto started = transport->start([this](const protocol::JsonRpcRequest& request,
                                                     const SessionContext& context) {
            return this->handle_request(request, context);
        });
        if (!started) {
            return std::unexpected(started.error());
        }
    }

    return core::Unit{};
}

void Server::stop() noexcept {
    for (auto& transport : transports_) {
        transport->stop();
    }
}

ServerBuilder& ServerBuilder::with_capabilities(protocol::ServerCapabilities capabilities) {
    options_.capabilities = capabilities;
    return *this;
}

ServerBuilder& ServerBuilder::with_transport(std::unique_ptr<Transport> transport) {
    transports_.push_back(std::move(transport));
    return *this;
}

ServerBuilder& ServerBuilder::with_auth_provider(std::unique_ptr<AuthProvider> auth_provider) {
    auth_provider_ = std::move(auth_provider);
    return *this;
}

ServerBuilder& ServerBuilder::with_rate_limiter(std::unique_ptr<RateLimiter> rate_limiter) {
    rate_limiter_ = std::move(rate_limiter);
    return *this;
}

core::Result<std::unique_ptr<Server>> ServerBuilder::build() {
    auto server = std::make_unique<Server>(options_);
    server->set_auth_provider(std::move(auth_provider_));
    server->set_rate_limiter(std::move(rate_limiter_));

    for (auto& transport : transports_) {
        const auto added = server->add_transport(std::move(transport));
        if (!added) {
            return std::unexpected(added.error());
        }
    }

    return server;
}

} // namespace mcp::server
