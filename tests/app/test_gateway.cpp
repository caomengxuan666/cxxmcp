// Copyright (c) 2025 [caomengxuan666]

#include <atomic>
#include <chrono>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "cxxmcp/app.hpp"
#include "cxxmcp/client.hpp"
#include "cxxmcp/gateway.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/server.hpp"
#include "httplib.h"

namespace {

using mcp::protocol::Json;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

mcp::app::Policy enabled_policy() {
  mcp::app::Policy policy;
  policy.enabled = true;
  policy.approval = mcp::app::ApprovalState::approved;
  policy.permissions.insert(mcp::app::Permission::filesystem_read);
  return policy;
}

mcp::app::DiscoveredCapability read_file_capability() {
  return mcp::app::DiscoveredCapability{
      .id = "server.filesystem:tool:read_file",
      .kind = mcp::app::CapabilityKind::tool,
      .server_id = "server.filesystem",
      .upstream_name = "read_file",
      .exposed_name = "filesystem.read_file",
      .title = "Read File",
      .description = "Read a local file",
      .uri = {},
      .input_schema = Json{{"type", "object"}},
      .output_schema = Json::object(),
      .template_text = {},
      .capability_hash = "hash-1",
  };
}

mcp::app::CapabilityBinding read_file_binding(bool enabled = true) {
  auto policy = enabled_policy();
  policy.enabled = enabled;
  return mcp::app::CapabilityBinding{
      .id = "binding.filesystem.read_file",
      .server_id = "server.filesystem",
      .kind = mcp::app::CapabilityKind::tool,
      .upstream_name = "read_file",
      .exposed_name = "filesystem.read_file",
      .namespace_strategy = mcp::app::NamespaceStrategy::server_prefix,
      .enabled = enabled,
      .policy = policy,
  };
}

mcp::app::DiscoveredCapability summarize_prompt_capability() {
  return mcp::app::DiscoveredCapability{
      .id = "server.filesystem:prompt:summarize",
      .kind = mcp::app::CapabilityKind::prompt,
      .server_id = "server.filesystem",
      .upstream_name = "summarize",
      .exposed_name = "filesystem.summarize",
      .title = "Summarize",
      .description = "Summarize text",
      .uri = {},
      .input_schema = Json{{"type", "object"}},
      .output_schema = Json::object(),
      .template_text = {},
      .capability_hash = "hash-prompt",
  };
}

mcp::app::DiscoveredCapability readme_resource_capability() {
  return mcp::app::DiscoveredCapability{
      .id = "server.filesystem:resource:file:///workspace/README.md",
      .kind = mcp::app::CapabilityKind::resource,
      .server_id = "server.filesystem",
      .upstream_name = "Readme",
      .exposed_name = "filesystem.Readme",
      .title = "Readme",
      .description = "Workspace readme",
      .uri = "file:///workspace/README.md",
      .input_schema = Json::object(),
      .output_schema = Json{{"mimeType", "text/markdown"}},
      .template_text = {},
      .capability_hash = "hash-resource",
  };
}

mcp::app::CapabilityBinding capability_binding(
    const mcp::app::DiscoveredCapability& capability) {
  return mcp::app::CapabilityBinding{
      .id = "binding." + capability.id,
      .server_id = capability.server_id,
      .kind = capability.kind,
      .upstream_name = capability.upstream_name,
      .exposed_name = capability.exposed_name,
      .namespace_strategy = mcp::app::NamespaceStrategy::server_prefix,
      .enabled = true,
      .policy = enabled_policy(),
  };
}

mcp::app::ExposureProfile dev_profile(
    std::vector<mcp::app::CapabilityBinding> bindings) {
  return mcp::app::ExposureProfile{
      .id = "profile.dev",
      .name = "Dev Gateway",
      .instructions = "Expose reviewed tools only.",
      .endpoint =
          mcp::app::HostedEndpoint{
              .name = "dev-http",
              .listen_host = "127.0.0.1",
              .listen_port = 8765,
              .path = "/cxxmcp/dev",
              .transport = mcp::app::McpServerTransportKind::streamable_http,
          },
      .bindings = std::move(bindings),
      .environment_overrides = {},
  };
}

mcp::app::McpServerDefinition child_process_server() {
  return mcp::app::McpServerDefinition{
      .id = "server.child",
      .name = "child",
      .display_name = "Child",
      .description = "Child test MCP server",
      .transport = mcp::app::McpServerTransportKind::stdio,
      .stdio =
          mcp::app::StdioLaunchConfig{
              .command = MCP_TEST_CHILD_EXE,
              .args = {},
              .cwd = {},
              .env = {},
          },
      .enabled = true,
      .auto_start = true,
      .trust = mcp::app::McpServerTrustState::trusted,
  };
}

mcp::app::DiscoveredCapability child_echo_capability() {
  return mcp::app::DiscoveredCapability{
      .id = "server.child:tool:echo",
      .kind = mcp::app::CapabilityKind::tool,
      .server_id = "server.child",
      .upstream_name = "echo",
      .exposed_name = "child.echo",
      .title = "Echo",
      .description = "Echo test tool",
      .uri = {},
      .input_schema = Json{{"type", "object"}},
      .output_schema = Json::object(),
      .template_text = {},
      .capability_hash = {},
  };
}

mcp::app::DiscoveredCapability child_summarize_prompt_capability() {
  auto capability = summarize_prompt_capability();
  capability.id = "server.child:prompt:summarize";
  capability.server_id = "server.child";
  capability.exposed_name = "child.summarize";
  return capability;
}

mcp::app::DiscoveredCapability child_readme_resource_capability() {
  auto capability = readme_resource_capability();
  capability.id = "server.child:resource:file:///workspace/README.md";
  capability.server_id = "server.child";
  capability.exposed_name = "child.Readme";
  return capability;
}

mcp::app::CapabilityBinding child_echo_binding() {
  auto policy = enabled_policy();
  return mcp::app::CapabilityBinding{
      .id = "binding.child.echo",
      .server_id = "server.child",
      .kind = mcp::app::CapabilityKind::tool,
      .upstream_name = "echo",
      .exposed_name = "child.echo",
      .namespace_strategy = mcp::app::NamespaceStrategy::server_prefix,
      .enabled = true,
      .policy = policy,
  };
}

struct RecordedCall {
  std::string server_id;
  std::string tool_name;
  Json arguments = Json::object();
};

bool wait_for_http_gateway(int port, std::string_view path) {
  for (int attempt = 0; attempt < 40; ++attempt) {
    mcp::client::Client client(std::make_unique<mcp::client::HttpTransport>(
        mcp::client::HttpTransportOptions{
            .host = "127.0.0.1",
            .port = port,
            .path = std::string(path),
            .headers = {},
            .timeout = std::chrono::milliseconds{100},
        }));

    const auto response =
        client.raw_request(mcp::protocol::make_ping_request(std::int64_t{1}));
    if (response.has_value()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
  }
  return false;
}

bool wait_for_http_gateway_shutdown(int port, std::string_view path) {
  for (int attempt = 0; attempt < 40; ++attempt) {
    mcp::client::Client client(std::make_unique<mcp::client::HttpTransport>(
        mcp::client::HttpTransportOptions{
            .host = "127.0.0.1",
            .port = port,
            .path = std::string(path),
            .headers = {},
            .timeout = std::chrono::milliseconds{100},
        }));

    const auto response =
        client.raw_request(mcp::protocol::make_ping_request(std::int64_t{1}));
    if (!response.has_value()) {
      return true;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{50});
  }
  return false;
}

class HttpUpstreamMcpServerFixture {
 public:
  HttpUpstreamMcpServerFixture() {
    server_.Post("/mcp", [this](const httplib::Request& request,
                                httplib::Response& response) {
      handle(request, response);
    });
    port_ = server_.bind_to_any_port("127.0.0.1");
    if (port_ <= 0) {
      throw std::runtime_error("failed to bind upstream http test server");
    }
    thread_ = std::thread([this]() { server_.listen_after_bind(); });
    server_.wait_until_ready();
  }

  ~HttpUpstreamMcpServerFixture() {
    server_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  std::string url() const {
    return "http://127.0.0.1:" + std::to_string(port_) + "/mcp";
  }

  bool authorization_header_seen() const {
    return authorization_header_seen_.load();
  }

 private:
  void handle(const httplib::Request& request, httplib::Response& response) {
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

    if (std::holds_alternative<mcp::protocol::JsonRpcNotification>(*parsed)) {
      response.status = 204;
      return;
    }

    const auto* rpc_request =
        std::get_if<mcp::protocol::JsonRpcRequest>(&*parsed);
    if (rpc_request == nullptr) {
      response.status = 400;
      response.set_content("{\"error\":\"unexpected message\"}",
                           "application/json");
      return;
    }

    if (rpc_request->method == mcp::protocol::InitializeMethod) {
      const auto serialized =
          mcp::protocol::serialize_response(mcp::protocol::JsonRpcResponse{
              .id = rpc_request->id,
              .result =
                  Json{
                      {"protocolVersion",
                       std::string(mcp::protocol::McpProtocolVersion)},
                      {"capabilities", Json{{"tools", Json::object()}}},
                      {"serverInfo",
                       Json{{"name", "http-upstream"}, {"version", "1"}}},
                  },
          });
      response.set_content(*serialized, "application/json");
      return;
    }

    if (rpc_request->method == "tools/call") {
      const auto serialized =
          mcp::protocol::serialize_response(mcp::protocol::JsonRpcResponse{
              .id = rpc_request->id,
              .result =
                  mcp::protocol::tool_result_to_json(mcp::protocol::ToolResult{
                      .content = {mcp::protocol::ContentBlock{
                          .type = "text",
                          .text = "http upstream",
                          .data = Json::object(),
                      }},
                      .structured_content = rpc_request->params.value(
                          "arguments", Json::object()),
                      .is_error = false,
                  }),
          });
      response.set_content(*serialized, "application/json");
      return;
    }

    const auto serialized =
        mcp::protocol::serialize_response(mcp::protocol::JsonRpcResponse{
            .id = rpc_request->id,
            .error =
                mcp::protocol::ErrorObject{
                    .code = static_cast<int>(
                        mcp::protocol::ErrorCode::MethodNotFound),
                    .message = "unexpected method",
                },
        });
    response.set_content(*serialized, "application/json");
  }

  httplib::Server server_;
  std::thread thread_;
  int port_ = 0;
  std::atomic<bool> authorization_header_seen_{false};
};

void test_gateway_lists_profile_bound_tools() {
  mcp::app::MemoryExposureProfileStore profiles(
      {dev_profile({read_file_binding()})});
  mcp::app::MemoryCapabilityCatalog capabilities({read_file_capability()});
  mcp::app::GatewayRoutingService gateway(
      profiles, capabilities,
      [](std::string_view, const mcp::protocol::ToolCall&)
          -> mcp::core::Result<mcp::protocol::ToolResult> {
        return std::unexpected(mcp::core::Error{1, "not used"});
      });

  const auto tools = gateway.list_tools("profile.dev");
  require(tools.has_value(), "gateway list_tools should succeed");
  require(tools->size() == 1, "gateway list_tools count mismatch");
  require(tools->front().name == "filesystem.read_file",
          "gateway exposed tool name mismatch");
  require(tools->front().description == "Read a local file",
          "gateway tool description mismatch");
  require(tools->front().input_schema.at("type") == "object",
          "gateway tool schema mismatch");
}

void test_gateway_routes_call_to_upstream_tool_name() {
  mcp::app::MemoryExposureProfileStore profiles(
      {dev_profile({read_file_binding()})});
  mcp::app::MemoryCapabilityCatalog capabilities({read_file_capability()});
  RecordedCall recorded;
  mcp::app::GatewayRoutingService gateway(
      profiles, capabilities,
      [&](std::string_view server_id, const mcp::protocol::ToolCall& call)
          -> mcp::core::Result<mcp::protocol::ToolResult> {
        recorded.server_id = std::string(server_id);
        recorded.tool_name = call.name;
        recorded.arguments = call.arguments;
        return mcp::protocol::ToolResult{
            .content = {mcp::protocol::ContentBlock{
                .type = "text",
                .text = "ok",
                .data = Json::object(),
            }},
            .structured_content = Json{{"routed", true}},
            .is_error = false,
        };
      });

  const auto result = gateway.call_tool("profile.dev", "filesystem.read_file",
                                        Json{{"path", "README.md"}});
  require(result.has_value(), "gateway call_tool should succeed");
  require(recorded.server_id == "server.filesystem",
          "gateway upstream server mismatch");
  require(recorded.tool_name == "read_file", "gateway upstream tool mismatch");
  require(recorded.arguments.at("path") == "README.md",
          "gateway upstream arguments mismatch");
  require(result->content.front().text == "ok", "gateway result mismatch");
}

void test_disabled_binding_is_not_exposed() {
  mcp::app::MemoryExposureProfileStore profiles(
      {dev_profile({read_file_binding(false)})});
  mcp::app::MemoryCapabilityCatalog capabilities({read_file_capability()});
  mcp::app::GatewayRoutingService gateway(
      profiles, capabilities,
      [](std::string_view, const mcp::protocol::ToolCall&)
          -> mcp::core::Result<mcp::protocol::ToolResult> {
        return std::unexpected(mcp::core::Error{1, "not used"});
      });

  const auto tools = gateway.list_tools("profile.dev");
  require(tools.has_value(), "disabled binding list should still succeed");
  require(tools->empty(), "disabled binding should not be listed");

  const auto called =
      gateway.call_tool("profile.dev", "filesystem.read_file", Json::object());
  require(!called.has_value(), "disabled binding call should fail");
  require(called.error().code ==
              static_cast<int>(mcp::protocol::ErrorCode::PermissionDenied),
          "disabled binding should fail with permission denied");
}

void test_gateway_readiness_reports_ready_profile() {
  mcp::app::MemoryExposureProfileStore profiles(
      {dev_profile({read_file_binding()})});
  mcp::app::MemoryCapabilityCatalog capabilities({read_file_capability()});
  auto server = child_process_server();
  server.id = "server.filesystem";
  mcp::app::MemoryMcpServerStore servers({server});
  mcp::app::GatewayReadinessService readiness(profiles, capabilities, servers);

  const auto report = readiness.check_profile("profile.dev");

  require(report.ready, "gateway readiness should report ready profile");
  require(report.binding_count == 1,
          "gateway readiness binding count mismatch");
  require(report.enabled_binding_count == 1,
          "gateway readiness enabled binding count mismatch");
  require(report.issues.empty(),
          "gateway readiness ready profile should not have issues");
}

void test_gateway_readiness_reports_runtime_unready_server() {
  mcp::app::MemoryExposureProfileStore profiles(
      {dev_profile({read_file_binding()})});
  mcp::app::MemoryCapabilityCatalog capabilities({read_file_capability()});
  auto server = child_process_server();
  server.id = "server.filesystem";
  mcp::app::MemoryMcpServerStore servers({server});
  mcp::app::GatewayReadinessService readiness(
      profiles, capabilities, servers, [] {
        return std::vector<mcp::app::GatewayServerHealth>{
            mcp::app::GatewayServerHealth{
                .server_id = "server.filesystem",
                .ready = false,
                .capability_count = 0,
                .error_message = "upstream initialize failed",
                .error_detail = "server.filesystem",
            },
        };
      });

  const auto report = readiness.check_profile("profile.dev");

  require(!report.ready,
          "gateway readiness should report runtime-unready profile");
  require(report.issues.size() == 1,
          "gateway runtime readiness issue count mismatch");
  require(report.issues[0].code == "server_unready",
          "gateway runtime readiness issue code mismatch");
  require(report.issues[0].message == "upstream initialize failed",
          "gateway runtime readiness issue message mismatch");
  require(report.issues[0].detail == "server.filesystem",
          "gateway runtime readiness issue detail mismatch");
}

void test_gateway_readiness_reports_upstream_issues() {
  mcp::app::MemoryExposureProfileStore profiles(
      {dev_profile({read_file_binding()})});
  mcp::app::MemoryCapabilityCatalog capabilities;
  auto server = child_process_server();
  server.id = "server.filesystem";
  server.trust = mcp::app::McpServerTrustState::untrusted;
  mcp::app::MemoryMcpServerStore servers({server});
  mcp::app::GatewayReadinessService readiness(profiles, capabilities, servers);

  const auto report = readiness.check_profile("profile.dev");

  require(!report.ready, "gateway readiness should report unready profile");
  require(report.issues.size() == 2, "gateway readiness issue count mismatch");
  require(report.issues[0].code == "capability_not_found",
          "gateway readiness capability issue mismatch");
  require(report.issues[1].code == "server_untrusted",
          "gateway readiness server issue mismatch");
}

void test_gateway_handler_lists_tools_for_downstream_request() {
  mcp::app::MemoryExposureProfileStore profiles(
      {dev_profile({read_file_binding()})});
  mcp::app::MemoryCapabilityCatalog capabilities({read_file_capability()});
  mcp::app::GatewayRoutingService gateway(
      profiles, capabilities,
      [](std::string_view, const mcp::protocol::ToolCall&)
          -> mcp::core::Result<mcp::protocol::ToolResult> {
        return std::unexpected(mcp::core::Error{1, "not used"});
      });
  mcp::app::GatewayRequestHandler handler(gateway, "profile.dev");

  const auto response = handler.handle(mcp::protocol::JsonRpcRequest{
      .method = "tools/list",
      .params = Json::object(),
      .id = std::int64_t{7},
  });

  require(response.has_value(), "gateway handler tools/list should succeed");
  require(response->result.has_value(),
          "gateway handler tools/list should return result");
  require(response->result->at("tools").size() == 1,
          "gateway handler tool count mismatch");
  require(response->result->at("tools").front().at("name") ==
              "filesystem.read_file",
          "gateway handler exposed tool mismatch");
  require(std::get<std::int64_t>(*response->id) == 7,
          "gateway handler response id mismatch");
}

void test_gateway_handler_initialize_and_ping() {
  mcp::app::MemoryExposureProfileStore profiles(
      {dev_profile({read_file_binding()})});
  mcp::app::MemoryCapabilityCatalog capabilities({read_file_capability()});
  mcp::app::GatewayRoutingService gateway(
      profiles, capabilities,
      [](std::string_view, const mcp::protocol::ToolCall&)
          -> mcp::core::Result<mcp::protocol::ToolResult> {
        return std::unexpected(mcp::core::Error{1, "not used"});
      });
  mcp::app::GatewayRequestHandler handler(gateway, "profile.dev");

  const auto initialized = handler.handle(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::InitializeMethod),
      .params = Json::object(),
      .id = std::string("init-1"),
  });
  require(initialized.has_value(), "gateway initialize should succeed");
  require(initialized->result.has_value(), "gateway initialize result missing");
  require(initialized->result->at("protocolVersion") ==
              std::string(mcp::protocol::McpProtocolVersion),
          "gateway initialize protocol mismatch");
  require(initialized->result->at("capabilities").contains("tools"),
          "gateway initialize tools capability missing");
  require(
      initialized->result->at("instructions") == "Expose reviewed tools only.",
      "gateway initialize instructions mismatch");

  const auto ping = handler.handle(mcp::protocol::JsonRpcRequest{
      .method = std::string(mcp::protocol::PingMethod),
      .params = Json::object(),
      .id = std::int64_t{8},
  });
  require(ping.has_value(), "gateway ping should succeed");
  require(ping->result.has_value(), "gateway ping result missing");
  require(ping->result->empty(), "gateway ping result should be empty");
}

void test_gateway_handler_lists_profile_bound_prompts_and_resources() {
  const auto prompt = summarize_prompt_capability();
  const auto resource = readme_resource_capability();
  mcp::app::MemoryExposureProfileStore profiles({dev_profile(
      {capability_binding(prompt), capability_binding(resource)})});
  mcp::app::MemoryCapabilityCatalog capabilities({prompt, resource});
  mcp::app::GatewayRoutingService gateway(
      profiles, capabilities,
      [](std::string_view, const mcp::protocol::ToolCall&)
          -> mcp::core::Result<mcp::protocol::ToolResult> {
        return std::unexpected(mcp::core::Error{1, "not used"});
      });
  mcp::app::GatewayRequestHandler handler(gateway, "profile.dev");

  const auto prompts = handler.handle(mcp::protocol::JsonRpcRequest{
      .method = "prompts/list",
      .params = Json::object(),
      .id = std::int64_t{11},
  });
  require(prompts.has_value(), "gateway handler prompts/list should succeed");
  require(prompts->result.has_value(),
          "gateway handler prompts/list result missing");
  require(prompts->result->at("prompts").size() == 1,
          "gateway handler prompt count mismatch");
  require(prompts->result->at("prompts").front().at("name") ==
              "filesystem.summarize",
          "gateway handler prompt name mismatch");

  const auto resources = handler.handle(mcp::protocol::JsonRpcRequest{
      .method = "resources/list",
      .params = Json::object(),
      .id = std::int64_t{12},
  });
  require(resources.has_value(),
          "gateway handler resources/list should succeed");
  require(resources->result.has_value(),
          "gateway handler resources/list result missing");
  require(resources->result->at("resources").size() == 1,
          "gateway handler resource count mismatch");
  require(resources->result->at("resources").front().at("uri") ==
              "file:///workspace/README.md",
          "gateway handler resource uri mismatch");
}

void test_gateway_handler_routes_tools_call_request() {
  mcp::app::MemoryExposureProfileStore profiles(
      {dev_profile({read_file_binding()})});
  mcp::app::MemoryCapabilityCatalog capabilities({read_file_capability()});
  RecordedCall recorded;
  mcp::app::GatewayRoutingService gateway(
      profiles, capabilities,
      [&](std::string_view server_id, const mcp::protocol::ToolCall& call)
          -> mcp::core::Result<mcp::protocol::ToolResult> {
        recorded.server_id = std::string(server_id);
        recorded.tool_name = call.name;
        recorded.arguments = call.arguments;
        return mcp::protocol::ToolResult{
            .content = {mcp::protocol::ContentBlock{
                .type = "text",
                .text = "routed",
                .data = Json::object(),
            }},
            .structured_content = std::nullopt,
            .is_error = false,
        };
      });
  mcp::app::GatewayRequestHandler handler(gateway, "profile.dev");

  const auto response = handler.handle(mcp::protocol::JsonRpcRequest{
      .method = "tools/call",
      .params =
          Json{
              {"name", "filesystem.read_file"},
              {"arguments", Json{{"path", "README.md"}}},
          },
      .id = std::string("call-1"),
  });

  require(response.has_value(), "gateway handler tools/call should succeed");
  require(response->result.has_value(),
          "gateway handler tools/call should return result");
  require(response->result->at("content").front().at("text") == "routed",
          "gateway handler result mismatch");
  require(recorded.server_id == "server.filesystem",
          "gateway handler server route mismatch");
  require(recorded.tool_name == "read_file",
          "gateway handler tool route mismatch");
  require(recorded.arguments.at("path") == "README.md",
          "gateway handler args mismatch");
}

void test_gateway_handler_rejects_invalid_tools_call_params() {
  mcp::app::MemoryExposureProfileStore profiles(
      {dev_profile({read_file_binding()})});
  mcp::app::MemoryCapabilityCatalog capabilities({read_file_capability()});
  mcp::app::GatewayRoutingService gateway(
      profiles, capabilities,
      [](std::string_view, const mcp::protocol::ToolCall&)
          -> mcp::core::Result<mcp::protocol::ToolResult> {
        return std::unexpected(mcp::core::Error{1, "not used"});
      });
  mcp::app::GatewayRequestHandler handler(gateway, "profile.dev");

  const auto response = handler.handle(mcp::protocol::JsonRpcRequest{
      .method = "tools/call",
      .params = Json{{"arguments", Json::object()}},
      .id = std::int64_t{9},
  });

  require(response.has_value(),
          "invalid request should produce JSON-RPC error response");
  require(response->error.has_value(),
          "invalid tools/call should return error");
  require(response->error->code ==
              static_cast<int>(mcp::protocol::ErrorCode::InvalidRequest),
          "invalid tools/call error code mismatch");
}

void test_stdio_gateway_routes_tools_call_to_child_process_server() {
  mcp::app::MemoryMcpServerStore servers({child_process_server()});
  mcp::app::MemoryExposureProfileStore profiles(
      {dev_profile({child_echo_binding()})});
  mcp::app::MemoryCapabilityCatalog capabilities({child_echo_capability()});
  mcp::app::GatewayRoutingService gateway(
      profiles, capabilities,
      mcp::app::make_process_gateway_tool_caller(servers));

  std::istringstream input(
      R"({"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"child.echo","arguments":{"value":42}}})"
      "\n");
  std::ostringstream output;
  const auto served =
      mcp::app::run_stdio_gateway(gateway, "profile.dev", input, output);
  require(served.has_value(), "stdio gateway should serve tools/call");

  const auto response = mcp::protocol::parse_response(output.str());
  require(response.has_value(), "stdio gateway response should parse");
  require(response->result.has_value(),
          "stdio gateway tools/call should return result");
  require(response->result->at("content").front().at("text") == "echo",
          "stdio gateway upstream result text mismatch");
  require(response->result->at("structuredContent").at("value") == 42,
          "stdio gateway upstream structured content mismatch");
}

void test_stdio_gateway_routes_prompt_and_resource_requests_to_child_process_server() {
  const auto prompt = child_summarize_prompt_capability();
  const auto resource = child_readme_resource_capability();
  mcp::app::MemoryMcpServerStore servers({child_process_server()});
  mcp::app::MemoryExposureProfileStore profiles({dev_profile(
      {capability_binding(prompt), capability_binding(resource)})});
  mcp::app::MemoryCapabilityCatalog capabilities({prompt, resource});
  mcp::app::GatewayRoutingService gateway(
      profiles, capabilities,
      mcp::app::make_process_gateway_tool_caller(servers),
      mcp::app::make_process_gateway_prompt_getter(servers),
      mcp::app::make_process_gateway_resource_reader(servers));

  std::istringstream input(
      R"({"jsonrpc":"2.0","id":20,"method":"prompts/get","params":{"name":"child.summarize","arguments":{"text":"hello"}}})"
      "\n"
      R"({"jsonrpc":"2.0","id":21,"method":"resources/read","params":{"uri":"file:///workspace/README.md"}})"
      "\n");
  std::ostringstream output;
  const auto served =
      mcp::app::run_stdio_gateway(gateway, "profile.dev", input, output);
  require(served.has_value(),
          "stdio gateway should serve prompt/resource requests");

  std::istringstream responses(output.str());
  std::string prompt_line;
  std::string resource_line;
  std::getline(responses, prompt_line);
  std::getline(responses, resource_line);

  const auto prompt_response = mcp::protocol::parse_response(prompt_line);
  require(prompt_response.has_value(),
          "stdio gateway prompt response should parse");
  require(prompt_response->result.has_value(),
          "stdio gateway prompts/get should return result");
  require(prompt_response->result->at("messages")
                  .front()
                  .at("content")
                  .at("text") == "Summarize hello",
          "stdio gateway upstream prompt text mismatch");

  const auto resource_response = mcp::protocol::parse_response(resource_line);
  require(resource_response.has_value(),
          "stdio gateway resource response should parse");
  require(resource_response->result.has_value(),
          "stdio gateway resources/read should return result");
  require(resource_response->result->at("contents").front().at("text") ==
              "hello from readme",
          "stdio gateway upstream resource text mismatch");
}

void test_gateway_rejects_untrusted_upstream_server() {
  auto server = child_process_server();
  server.trust = mcp::app::McpServerTrustState::untrusted;
  mcp::app::MemoryMcpServerStore servers({server});

  const auto caller = mcp::app::make_process_gateway_tool_caller(servers);
  const auto result =
      caller("server.child", mcp::protocol::ToolCall{
                                 .name = "echo",
                                 .arguments = Json{{"message", "blocked"}},
                             });

  require(!result.has_value(), "untrusted upstream server call should fail");
  require(result.error().message == "cxxmcp server is untrusted",
          "untrusted upstream error mismatch");
}

void test_gateway_rejects_blocked_upstream_server() {
  auto server = child_process_server();
  server.trust = mcp::app::McpServerTrustState::blocked;
  mcp::app::MemoryMcpServerStore servers({server});

  const auto caller = mcp::app::make_process_gateway_tool_caller(servers);
  const auto result =
      caller("server.child", mcp::protocol::ToolCall{
                                 .name = "echo",
                                 .arguments = Json{{"message", "blocked"}},
                             });

  require(!result.has_value(), "blocked upstream server call should fail");
  require(result.error().message == "cxxmcp server is blocked",
          "blocked upstream error mismatch");
}

void test_http_gateway_exposes_profile_tools() {
  auto profile = dev_profile({read_file_binding()});
  profile.endpoint.listen_port = 39917;
  profile.endpoint.path = "/cxxmcp/http-test";

  mcp::app::MemoryExposureProfileStore profiles({profile});
  mcp::app::MemoryCapabilityCatalog capabilities({read_file_capability()});
  RecordedCall recorded;
  mcp::app::GatewayRoutingService gateway(
      profiles, capabilities,
      [&](std::string_view server_id, const mcp::protocol::ToolCall& call)
          -> mcp::core::Result<mcp::protocol::ToolResult> {
        recorded.server_id = std::string(server_id);
        recorded.tool_name = call.name;
        recorded.arguments = call.arguments;
        return mcp::protocol::ToolResult{
            .content = {mcp::protocol::ContentBlock{
                .type = "text",
                .text = "http routed",
                .data = Json::object(),
            }},
            .structured_content = Json{{"ok", true}},
            .is_error = false,
        };
      });
  mcp::app::GatewayRequestHandler request_handler(gateway, profile.id);
  mcp::server::HttpTransport transport(mcp::server::HttpTransportOptions{
      .listen_host = profile.endpoint.listen_host,
      .listen_port = profile.endpoint.listen_port,
      .path = profile.endpoint.path,
  });

  std::optional<mcp::core::Error> server_error;
  std::thread server_thread([&]() {
    const auto served =
        transport.start([&](const mcp::protocol::JsonRpcRequest& request,
                            const mcp::server::SessionContext&) {
          return request_handler.handle(request);
        });
    if (!served) {
      server_error = served.error();
    }
  });
  struct ThreadCleanup {
    mcp::server::HttpTransport& transport;
    std::thread& thread;
    ~ThreadCleanup() {
      transport.stop();
      if (thread.joinable()) {
        thread.join();
      }
    }
  } cleanup{transport, server_thread};

  require(wait_for_http_gateway(profile.endpoint.listen_port,
                                profile.endpoint.path),
          "http gateway should become reachable");

  mcp::client::Client client(std::make_unique<mcp::client::HttpTransport>(
      mcp::client::HttpTransportOptions{
          .host = "127.0.0.1",
          .port = profile.endpoint.listen_port,
          .path = profile.endpoint.path,
      }));
  const auto tools = client.list_tools();
  require(tools.has_value(), "http gateway tools/list should succeed");
  require(tools->size() == 1, "http gateway tools/list count mismatch");
  require(tools->front().name == "filesystem.read_file",
          "http gateway exposed tool mismatch");

  const auto called = client.call_tool(mcp::protocol::ToolCall{
      .name = "filesystem.read_file",
      .arguments = Json{{"path", "README.md"}},
  });
  require(called.has_value(), "http gateway tools/call should succeed");
  require(called->content.front().text == "http routed",
          "http gateway call result mismatch");
  require(recorded.server_id == "server.filesystem",
          "http gateway routed server mismatch");
  require(recorded.tool_name == "read_file",
          "http gateway upstream tool mismatch");
  require(recorded.arguments.at("path") == "README.md",
          "http gateway args mismatch");

  transport.stop();
  if (server_thread.joinable()) {
    server_thread.join();
  }
  require(!server_error.has_value(),
          "http gateway transport should stop cleanly");
}

void test_http_gateway_routes_to_http_upstream_server() {
  HttpUpstreamMcpServerFixture upstream;
  mcp::app::McpServerDefinition upstream_server{
      .id = "server.http",
      .name = "http",
      .display_name = "HTTP",
      .description = "HTTP upstream MCP server",
      .transport = mcp::app::McpServerTransportKind::streamable_http,
      .stdio = {},
      .http =
          mcp::app::HttpConnectionConfig{
              .url = upstream.url(),
              .headers = {{"Authorization", "Bearer token"}},
          },
      .enabled = true,
      .auto_start = true,
      .trust = mcp::app::McpServerTrustState::trusted,
      .tags = {},
  };
  mcp::app::DiscoveredCapability capability{
      .id = "server.http:tool:echo",
      .kind = mcp::app::CapabilityKind::tool,
      .server_id = "server.http",
      .upstream_name = "echo",
      .exposed_name = "http.echo",
      .title = "HTTP Echo",
      .description = "Echo over HTTP",
      .uri = {},
      .input_schema = Json{{"type", "object"}},
      .output_schema = Json::object(),
      .template_text = {},
      .capability_hash = "http-hash",
  };
  auto profile = dev_profile({capability_binding(capability)});
  profile.endpoint.listen_port = 39923;
  profile.endpoint.path = "/cxxmcp/http-upstream-test";

  mcp::app::MemoryMcpServerStore servers({upstream_server});
  mcp::app::MemoryExposureProfileStore profiles({profile});
  mcp::app::MemoryCapabilityCatalog capabilities({capability});
  mcp::app::GatewayRoutingService gateway(
      profiles, capabilities,
      mcp::app::make_upstream_gateway_tool_caller(servers));
  mcp::app::GatewayRequestHandler request_handler(gateway, profile.id);
  mcp::server::HttpTransport transport(mcp::server::HttpTransportOptions{
      .listen_host = profile.endpoint.listen_host,
      .listen_port = profile.endpoint.listen_port,
      .path = profile.endpoint.path,
  });

  std::optional<mcp::core::Error> server_error;
  std::thread server_thread([&]() {
    const auto served =
        transport.start([&](const mcp::protocol::JsonRpcRequest& request,
                            const mcp::server::SessionContext&) {
          return request_handler.handle(request);
        });
    if (!served) {
      server_error = served.error();
    }
  });
  struct ThreadCleanup {
    mcp::server::HttpTransport& transport;
    std::thread& thread;
    ~ThreadCleanup() {
      transport.stop();
      if (thread.joinable()) {
        thread.join();
      }
    }
  } cleanup{transport, server_thread};

  require(wait_for_http_gateway(profile.endpoint.listen_port,
                                profile.endpoint.path),
          "http downstream gateway should become reachable");

  mcp::client::Client client(std::make_unique<mcp::client::HttpTransport>(
      mcp::client::HttpTransportOptions{
          .host = "127.0.0.1",
          .port = profile.endpoint.listen_port,
          .path = profile.endpoint.path,
      }));
  const auto called = client.call_tool(mcp::protocol::ToolCall{
      .name = "http.echo",
      .arguments = Json{{"value", 42}},
  });
  require(called.has_value(),
          "http gateway should route tools/call to http upstream");
  require(called->content.front().text == "http upstream",
          "http upstream result text mismatch");
  require(called->structured_content.has_value(),
          "http upstream structured content missing");
  require(called->structured_content->at("value") == 42,
          "http upstream arguments mismatch");
  require(upstream.authorization_header_seen(),
          "http gateway should send configured upstream headers");

  transport.stop();
  if (server_thread.joinable()) {
    server_thread.join();
  }
  require(!server_error.has_value(),
          "http upstream gateway transport should stop cleanly");
}

void test_gateway_runtime_manager_starts_and_stops_http_gateway() {
  auto profile = dev_profile({read_file_binding()});
  profile.endpoint.listen_port = 39931;
  profile.endpoint.path = "/cxxmcp/runtime-manager";

  mcp::app::MemoryExposureProfileStore profiles({profile});
  mcp::app::MemoryCapabilityCatalog capabilities({read_file_capability()});
  mcp::app::GatewayRuntimeManager runtime(
      profiles, capabilities,
      [&](std::string_view server_id, const mcp::protocol::ToolCall& call)
          -> mcp::core::Result<mcp::protocol::ToolResult> {
        require(server_id == "server.filesystem",
                "runtime manager server id mismatch");
        require(call.name == "read_file", "runtime manager tool name mismatch");
        return mcp::protocol::ToolResult{
            .content = {mcp::protocol::ContentBlock{
                .type = "text",
                .text = "runtime routed",
                .data = Json::object(),
            }},
            .structured_content = Json{{"ok", true}},
            .is_error = false,
        };
      });

  const auto started = runtime.start_http_gateway("profile.dev");
  require(started.has_value(), "runtime manager should start gateway");
  const auto snapshot = runtime.list_http_gateways();
  require(snapshot.size() == 1, "runtime manager snapshot count mismatch");
  require(snapshot.front().running,
          "runtime manager should report running gateway");
  require(snapshot.front().endpoint.listen_port == 39931,
          "runtime manager endpoint port mismatch");

  require(wait_for_http_gateway(profile.endpoint.listen_port,
                                profile.endpoint.path),
          "managed http gateway should become reachable");

  mcp::client::Client client(std::make_unique<mcp::client::HttpTransport>(
      mcp::client::HttpTransportOptions{
          .host = "127.0.0.1",
          .port = profile.endpoint.listen_port,
          .path = profile.endpoint.path,
      }));
  const auto tools = client.list_tools();
  require(tools.has_value(), "managed http gateway tools/list should succeed");
  require(tools->size() == 1, "managed http gateway tools/list count mismatch");
  require(tools->front().name == "filesystem.read_file",
          "managed http gateway tool mismatch");

  const auto stopped = runtime.stop_http_gateway("profile.dev");
  require(stopped.has_value(), "runtime manager should stop gateway");
  require(wait_for_http_gateway_shutdown(profile.endpoint.listen_port,
                                         profile.endpoint.path),
          "managed http gateway should stop");

  const auto stopped_snapshot = runtime.list_http_gateways();
  require(stopped_snapshot.size() == 1,
          "stopped runtime manager snapshot count mismatch");
  require(!stopped_snapshot.front().running,
          "runtime manager should report stopped gateway");
}

void test_public_gateway_runtime_builder_smoke() {
  auto runtime = mcp::gateway::Runtime::builder()
                     .profile("profile.public")
                     .host("127.0.0.1")
                     .port(39951)
                     .path("/cxxmcp/public")
                     .instruction("Expose reviewed tools only.")
                     .trust(true)
                     .discover(true)
                     .bind_server("server.child")
                     .add_stdio_server("server.child", MCP_TEST_CHILD_EXE, {})
                     .build();

  require(runtime.has_value(), "public gateway runtime should build");
  require(runtime->start().has_value(), "public gateway runtime should start");
  require(wait_for_http_gateway(39951, "/cxxmcp/public"),
          "public gateway should become reachable");

  mcp::client::Client client(std::make_unique<mcp::client::HttpTransport>(
      mcp::client::HttpTransportOptions{
          .host = "127.0.0.1",
          .port = 39951,
          .path = "/cxxmcp/public",
          .headers = {},
          .timeout = std::chrono::milliseconds{1000},
      }));

  const auto ping =
      client.raw_request(mcp::protocol::make_ping_request(std::int64_t{1}));
  require(ping.has_value(), "public gateway ping should succeed");
  require(ping->empty(), "public gateway ping result should be empty");

  const auto tools = client.list_tools();
  require(tools.has_value(), "public gateway tools/list should succeed");
  require(!tools->empty(), "public gateway should expose at least one tool");

  require(runtime->stop().has_value(), "public gateway runtime should stop");
  require(wait_for_http_gateway_shutdown(39951, "/cxxmcp/public"),
          "public gateway should stop accepting requests");
}

}  // namespace

int main() {
  const std::vector<std::pair<std::string_view, void (*)()>> tests = {
      {"gateway lists profile-bound tools",
       test_gateway_lists_profile_bound_tools},
      {"gateway routes calls", test_gateway_routes_call_to_upstream_tool_name},
      {"disabled binding is not exposed", test_disabled_binding_is_not_exposed},
      {"gateway readiness reports ready profile",
       test_gateway_readiness_reports_ready_profile},
      {"gateway readiness reports runtime-unready server",
       test_gateway_readiness_reports_runtime_unready_server},
      {"gateway readiness reports upstream issues",
       test_gateway_readiness_reports_upstream_issues},
      {"gateway handler lists tools",
       test_gateway_handler_lists_tools_for_downstream_request},
      {"gateway handler initialize and ping",
       test_gateway_handler_initialize_and_ping},
      {"gateway handler lists prompts and resources",
       test_gateway_handler_lists_profile_bound_prompts_and_resources},
      {"gateway handler routes tools/call",
       test_gateway_handler_routes_tools_call_request},
      {"gateway handler rejects invalid tools/call",
       test_gateway_handler_rejects_invalid_tools_call_params},
      {"stdio gateway routes tools/call to child process server",
       test_stdio_gateway_routes_tools_call_to_child_process_server},
      {"stdio gateway routes prompt/resource requests to child process server",
       test_stdio_gateway_routes_prompt_and_resource_requests_to_child_process_server},
      {"gateway rejects untrusted upstream server",
       test_gateway_rejects_untrusted_upstream_server},
      {"gateway rejects blocked upstream server",
       test_gateway_rejects_blocked_upstream_server},
      {"http gateway exposes profile tools",
       test_http_gateway_exposes_profile_tools},
      {"http gateway routes to http upstream server",
       test_http_gateway_routes_to_http_upstream_server},
      {"gateway runtime manager starts and stops http gateway",
       test_gateway_runtime_manager_starts_and_stops_http_gateway},
      {"public gateway runtime builder smoke",
       test_public_gateway_runtime_builder_smoke},
  };

  std::size_t failures = 0;
  for (const auto& [name, test] : tests) {
    try {
      test();
      std::cout << "[PASS] " << name << '\n';
    } catch (const std::exception& ex) {
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
