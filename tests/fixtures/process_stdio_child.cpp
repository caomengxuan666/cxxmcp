#include "mcp/protocol/serialization.hpp"
#include "mcp/protocol/prompt.hpp"
#include "mcp/protocol/resource.hpp"
#include "mcp/protocol/tool.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <variant>

namespace {

using mcp::protocol::Json;

void write_response(const mcp::protocol::JsonRpcResponse& response) {
    const auto serialized = mcp::protocol::serialize_response(response);
    if (!serialized) {
        return;
    }
    std::cout << *serialized << '\n';
    std::cout.flush();
}

void write_error(const mcp::protocol::JsonRpcRequest& request, mcp::protocol::ErrorCode code, std::string message) {
    write_response(mcp::protocol::make_error_response(
        std::optional<mcp::protocol::RequestId>{request.id},
        mcp::protocol::make_error(code, std::move(message))));
}

void handle_request(const mcp::protocol::JsonRpcRequest& request) {
    if (request.method == mcp::protocol::InitializeMethod) {
        write_response(mcp::protocol::make_response(
            request.id,
            Json{
                {"protocolVersion", std::string(mcp::protocol::McpProtocolVersion)},
                {"capabilities", Json{{"tools", Json::object()}}},
                {"serverInfo", Json{{"name", "process-stdio-child"}, {"version", "1"}}},
            }));
        return;
    }

    if (request.method == "tools/list") {
        write_response(mcp::protocol::make_response(
            request.id,
            Json{
                {"tools", Json::array({
                    Json{
                        {"name", "echo"},
                        {"description", "Echo test tool"},
                        {"inputSchema", Json{{"type", "object"}}},
                    },
                })},
            }));
        return;
    }

    if (request.method == "prompts/list") {
        write_response(mcp::protocol::make_response(
            request.id,
            Json{
                {"prompts", Json::array({
                    Json{
                        {"name", "summarize"},
                        {"description", "Summarize test prompt"},
                        {"arguments", Json::array({
                            Json{{"name", "text"}, {"description", "Text to summarize"}, {"required", true}},
                        })},
                    },
                })},
            }));
        return;
    }

    if (request.method == "prompts/get") {
        write_response(mcp::protocol::make_response(
            request.id,
            mcp::protocol::prompts_get_result_to_json(mcp::protocol::PromptsGetResult{
                .description = "Summarize test prompt",
                .messages = {mcp::protocol::PromptMessage{
                    .role = "user",
                    .content = mcp::protocol::ContentBlock{
                        .type = "text",
                        .text = "Summarize " + request.params.value("arguments", Json::object()).value("text", ""),
                        .data = Json::object(),
                    },
                }},
            })));
        return;
    }

    if (request.method == "resources/list") {
        write_response(mcp::protocol::make_response(
            request.id,
            Json{
                {"resources", Json::array({
                    Json{
                        {"uri", "file:///workspace/README.md"},
                        {"name", "Readme"},
                        {"description", "Workspace readme"},
                        {"mimeType", "text/markdown"},
                    },
                })},
            }));
        return;
    }

    if (request.method == "resources/read") {
        write_response(mcp::protocol::make_response(
            request.id,
            mcp::protocol::resources_read_result_to_json(mcp::protocol::ResourcesReadResult{
                .contents = {mcp::protocol::ResourceContents{
                    .uri = request.params.value("uri", ""),
                    .mime_type = "text/markdown",
                    .text = "hello from readme",
                    .blob = std::nullopt,
                }},
            })));
        return;
    }

    if (request.method == "tools/call") {
        write_response(mcp::protocol::make_response(
            request.id,
            mcp::protocol::tool_result_to_json(mcp::protocol::ToolResult{
                .content = {mcp::protocol::ContentBlock{
                    .type = "text",
                    .text = request.params.value("name", ""),
                    .data = Json::object(),
                }},
                .structured_content = request.params.value("arguments", Json::object()),
                .is_error = false,
            })));
        return;
    }

    write_error(request, mcp::protocol::ErrorCode::MethodNotFound, "method not found");
}

} // namespace

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }

        const auto message = mcp::protocol::parse_message(line);
        if (!message) {
            continue;
        }
        if (const auto* request = std::get_if<mcp::protocol::JsonRpcRequest>(&*message)) {
            handle_request(*request);
        }
    }

    return 0;
}
