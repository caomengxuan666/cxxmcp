#include "cxxmcp/app.hpp"
#include "cxxmcp/protocol/serialization.hpp"

#include "httplib.h"

#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

    using mcp::protocol::Json;

    void require(bool condition, std::string_view message) {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    class RecordingTransport final : public mcp::client::Transport {
    public:
        mcp::core::Result<mcp::protocol::JsonRpcResponse> send(const mcp::protocol::JsonRpcRequest &request) override {
            requests.push_back(request);
            if (request.method == "initialize") {
                return mcp::protocol::JsonRpcResponse{
                        .id = request.id,
                        .result = Json{
                                {"protocolVersion", std::string(mcp::protocol::McpProtocolVersion)},
                                {"capabilities", Json{{"tools", Json::object()}, {"prompts", Json::object()}, {"resources", Json::object()}}},
                                {"serverInfo", Json{{"name", "fake"}, {"version", "1"}}},
                        },
                        .error = std::nullopt,
                };
            }
            if (request.method == "tools/list") {
                return mcp::protocol::JsonRpcResponse{
                        .id = request.id,
                        .result = Json{
                                {"tools", Json::array({
                                                  Json{
                                                          {"name", "read_file"},
                                                          {"description", "Read a file"},
                                                          {"inputSchema", Json{{"type", "object"}}},
                                                  },
                                          })},
                        },
                        .error = std::nullopt,
                };
            }
            if (request.method == "prompts/list") {
                return mcp::protocol::JsonRpcResponse{
                        .id = request.id,
                        .result = Json{
                                {"prompts", Json::array({
                                                    Json{
                                                            {"name", "summarize"},
                                                            {"description", "Summarize input"},
                                                    },
                                            })},
                        },
                        .error = std::nullopt,
                };
            }
            if (request.method == "resources/list") {
                return mcp::protocol::JsonRpcResponse{
                        .id = request.id,
                        .result = Json{
                                {"resources", Json::array({
                                                      Json{
                                                              {"uri", "file:///workspace/README.md"},
                                                              {"name", "Readme"},
                                                              {"mimeType", "text/markdown"},
                                                      },
                                              })},
                        },
                        .error = std::nullopt,
                };
            }
            return std::unexpected(mcp::core::Error{1, "unexpected request", request.method});
        }

        mcp::core::Result<mcp::core::Unit> send_notification(
                const mcp::protocol::JsonRpcNotification &notification) override {
            notifications.push_back(notification);
            return mcp::core::Unit{};
        }

        std::vector<mcp::protocol::JsonRpcRequest> requests;
        std::vector<mcp::protocol::JsonRpcNotification> notifications;
    };

    class HttpMcpServerFixture {
    public:
        HttpMcpServerFixture() {
            server_.Post("/mcp", [this](const httplib::Request &request, httplib::Response &response) {
                handle(request, response);
            });
            port_ = server_.bind_to_any_port("127.0.0.1");
            if (port_ <= 0) {
                throw std::runtime_error("failed to bind test http server");
            }
            thread_ = std::thread([this]() {
                server_.listen_after_bind();
            });
            server_.wait_until_ready();
        }

        ~HttpMcpServerFixture() {
            server_.stop();
            if (thread_.joinable()) {
                thread_.join();
            }
        }

        int port() const {
            return port_;
        }

        bool initialized() const {
            return initialized_.load();
        }

        bool initialized_notification() const {
            return initialized_notification_.load();
        }

        bool authorization_header_seen() const {
            return authorization_header_seen_.load();
        }

    private:
        void handle(const httplib::Request &request, httplib::Response &response) {
            if (request.has_header("Authorization") &&
                request.get_header_value("Authorization") == "Bearer token") {
                authorization_header_seen_.store(true);
            }

            const auto parsed = mcp::protocol::parse_message(request.body);
            if (!parsed) {
                response.status = 400;
                response.set_content("{\"error\":\"bad request\"}", "application/json");
                return;
            }

            if (const auto *notification = std::get_if<mcp::protocol::JsonRpcNotification>(&*parsed)) {
                if (notification->method == mcp::protocol::InitializedMethod) {
                    initialized_notification_.store(true);
                }
                response.status = 204;
                return;
            }

            const auto *rpc_request = std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed);
            if (rpc_request == nullptr) {
                response.status = 400;
                response.set_content("{\"error\":\"unexpected message\"}", "application/json");
                return;
            }

            if (rpc_request->method == mcp::protocol::InitializeMethod) {
                initialized_.store(true);
                const auto serialized = mcp::protocol::serialize_response(mcp::protocol::JsonRpcResponse{
                        .id = rpc_request->id,
                        .result = mcp::protocol::Json{
                                {"protocolVersion", std::string(mcp::protocol::McpProtocolVersion)},
                                {"capabilities", mcp::protocol::Json{{"tools", mcp::protocol::Json::object()},
                                                                     {"prompts", mcp::protocol::Json::object()},
                                                                     {"resources", mcp::protocol::Json::object()}}},
                                {"serverInfo", mcp::protocol::Json{{"name", "fake-http"}, {"version", "1"}}},
                        },
                });
                response.set_content(*serialized, "application/json");
                return;
            }

            if (rpc_request->method == "tools/list") {
                const auto serialized = mcp::protocol::serialize_response(mcp::protocol::JsonRpcResponse{
                        .id = rpc_request->id,
                        .result = mcp::protocol::Json{
                                {"tools", mcp::protocol::Json::array({
                                                  mcp::protocol::Json{
                                                          {"name", "read_file"},
                                                          {"description", "Read a file"},
                                                          {"inputSchema", mcp::protocol::Json{{"type", "object"}}},
                                                  },
                                          })},
                        },
                });
                response.set_content(*serialized, "application/json");
                return;
            }

            if (rpc_request->method == "prompts/list") {
                const auto serialized = mcp::protocol::serialize_response(mcp::protocol::JsonRpcResponse{
                        .id = rpc_request->id,
                        .result = mcp::protocol::Json{
                                {"prompts", mcp::protocol::Json::array({
                                                    mcp::protocol::Json{
                                                            {"name", "summarize"},
                                                            {"description", "Summarize input"},
                                                    },
                                            })},
                        },
                });
                response.set_content(*serialized, "application/json");
                return;
            }

            if (rpc_request->method == "resources/list") {
                const auto serialized = mcp::protocol::serialize_response(mcp::protocol::JsonRpcResponse{
                        .id = rpc_request->id,
                        .result = mcp::protocol::Json{
                                {"resources", mcp::protocol::Json::array({
                                                      mcp::protocol::Json{
                                                              {"uri", "file:///workspace/README.md"},
                                                              {"name", "Readme"},
                                                              {"mimeType", "text/markdown"},
                                                      },
                                              })},
                        },
                });
                response.set_content(*serialized, "application/json");
                return;
            }

            const auto serialized = mcp::protocol::serialize_response(mcp::protocol::JsonRpcResponse{
                    .id = rpc_request->id,
                    .error = mcp::protocol::ErrorObject{
                            .code = static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
                            .message = "unexpected method",
                    },
            });
            response.set_content(*serialized, "application/json");
        }

        httplib::Server server_;
        std::thread thread_;
        int port_ = 0;
        std::atomic<bool> initialized_{false};
        std::atomic<bool> initialized_notification_{false};
        std::atomic<bool> authorization_header_seen_{false};
    };

    mcp::app::McpServerDefinition filesystem_server() {
        return mcp::app::McpServerDefinition{
                .id = "server.filesystem",
                .name = "filesystem",
                .display_name = "Filesystem",
                .description = "Filesystem MCP server",
                .transport = mcp::app::McpServerTransportKind::stdio,
                .stdio = mcp::app::StdioLaunchConfig{
                        .command = "npx",
                        .args = {"-y", "@modelcontextprotocol/server-filesystem", "C:/workspace"},
                        .cwd = {},
                        .env = {},
                },
                .http = {},
                .enabled = true,
                .auto_start = true,
                .trust = mcp::app::McpServerTrustState::trusted,
                .tags = {},
        };
    }

    mcp::app::McpServerDefinition child_process_server() {
        auto server = filesystem_server();
        server.id = "server.child";
        server.name = "child";
        server.stdio.command = MCP_TEST_CHILD_EXE;
        server.stdio.args = {};
        server.stdio.cwd = {};
        return server;
    }

    void test_client_discovery_session_drives_mcp_lifecycle_and_discovery() {
        auto transport = std::make_unique<RecordingTransport>();
        auto *recording = transport.get();
        mcp::app::ClientDiscoverySession session(std::move(transport));

        const auto initialized = session.initialize();
        require(initialized.has_value(), "client discovery initialize should succeed");
        require(recording->requests.size() == 1, "initialize should send one request");
        require(recording->requests.front().method == "initialize", "initialize method mismatch");
        require(recording->notifications.size() == 1, "initialized notification missing");
        require(recording->notifications.front().method == "notifications/initialized",
                "initialized notification method mismatch");

        const auto tools = session.discover_tools();
        require(tools.has_value(), "discover tools should succeed");
        require(tools->front().name == "read_file", "tool discovery mismatch");

        const auto prompts = session.discover_prompts();
        require(prompts.has_value(), "discover prompts should succeed");
        require(prompts->front().name == "summarize", "prompt discovery mismatch");

        const auto resources = session.discover_resources();
        require(resources.has_value(), "discover resources should succeed");
        require(resources->front().uri == "file:///workspace/README.md", "resource discovery mismatch");
    }

    void test_server_management_can_use_client_discovery_session() {
        mcp::app::MemoryMcpServerStore servers({filesystem_server()});
        mcp::app::MemoryCapabilityCatalog capabilities;
        mcp::app::MemoryExposureProfileStore exposure_profiles;
        mcp::app::ServerManagementService management(
                servers,
                capabilities,
                exposure_profiles,
                [](const mcp::app::McpServerDefinition &)
                        -> mcp::core::Result<std::unique_ptr<mcp::app::McpDiscoverySession>> {
                    return std::make_unique<mcp::app::ClientDiscoverySession>(std::make_unique<RecordingTransport>());
                });

        const auto discovered = management.discover_server("server.filesystem");
        require(discovered.has_value(), "server management discovery should succeed");
        require(discovered->capability_count == 3, "server management capability count mismatch");
        require(capabilities.list_capabilities().size() == 3, "capability catalog count mismatch");
    }

    void test_server_management_factory_starts_process_stdio_server() {
        mcp::app::MemoryMcpServerStore servers({child_process_server()});
        mcp::app::MemoryCapabilityCatalog capabilities;
        mcp::app::MemoryExposureProfileStore exposure_profiles;
        mcp::app::ServerManagementService management(
                servers,
                capabilities,
                exposure_profiles,
                [](const mcp::app::McpServerDefinition &server)
                        -> mcp::core::Result<std::unique_ptr<mcp::app::McpDiscoverySession>> {
                    return mcp::app::make_client_discovery_session_for_server(server);
                });

        const auto discovered = management.discover_server("server.child");
        require(discovered.has_value(), "process stdio discovery should succeed");
        require(discovered->capability_count == 3, "process stdio discovery count mismatch");
        const auto listed = capabilities.list_capabilities();
        require(listed.size() == 3, "process stdio capability catalog count mismatch");
        require(listed.front().upstream_name == "echo", "process stdio discovered tool mismatch");
        require(listed.front().exposed_name == "child.echo", "process stdio exposed name mismatch");
    }

    void test_server_management_factory_supports_http_server() {
        HttpMcpServerFixture fixture;
        mcp::app::McpServerDefinition server{
                .id = "server.http",
                .name = "http",
                .display_name = "HTTP",
                .description = "HTTP MCP server",
                .transport = mcp::app::McpServerTransportKind::streamable_http,
                .stdio = {},
                .http = mcp::app::HttpConnectionConfig{
                        .url = "http://127.0.0.1:" + std::to_string(fixture.port()) + "/mcp",
                        .headers = {{"Authorization", "Bearer token"}},
                },
                .enabled = true,
                .auto_start = true,
                .trust = mcp::app::McpServerTrustState::trusted,
                .tags = {"remote"},
        };

        mcp::app::MemoryMcpServerStore servers({server});
        mcp::app::MemoryCapabilityCatalog capabilities;
        mcp::app::MemoryExposureProfileStore exposure_profiles;
        mcp::app::ServerManagementService management(
                servers,
                capabilities,
                exposure_profiles,
                [](const mcp::app::McpServerDefinition &server)
                        -> mcp::core::Result<std::unique_ptr<mcp::app::McpDiscoverySession>> {
                    return mcp::app::make_client_discovery_session_for_server(server);
                });

        const auto discovered = management.discover_server("server.http");
        require(discovered.has_value(), "http discovery should succeed");
        require(discovered->capability_count == 3, "http discovery capability count mismatch");
        require(fixture.initialized(), "http server should receive initialize");
        require(fixture.initialized_notification(), "http server should receive initialized notification");
        require(fixture.authorization_header_seen(), "http discovery should send configured headers");
        require(capabilities.list_capabilities().size() == 3, "http capability catalog count mismatch");
    }

}// namespace

int main() {
    const std::vector<std::pair<std::string_view, void (*)()>> tests = {
            {"client discovery lifecycle", test_client_discovery_session_drives_mcp_lifecycle_and_discovery},
            {"server management uses client discovery", test_server_management_can_use_client_discovery_session},
            {"server management starts process stdio server", test_server_management_factory_starts_process_stdio_server},
            {"server management supports http server", test_server_management_factory_supports_http_server},
    };

    std::size_t failures = 0;
    for (const auto &[name, test]: tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception &ex) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << ex.what() << '\n';
        }
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << tests.size() << " test(s) passed\n";
    return 0;
}
