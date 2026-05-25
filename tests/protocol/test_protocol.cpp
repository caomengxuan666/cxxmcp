// Copyright (c) 2025 [caomengxuan666]

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cxxmcp/protocol/capabilities.hpp"
#include "cxxmcp/protocol/completion.hpp"
#include "cxxmcp/protocol/elicitation.hpp"
#include "cxxmcp/protocol/logging.hpp"
#include "cxxmcp/protocol/prompt.hpp"
#include "cxxmcp/protocol/resource.hpp"
#include "cxxmcp/protocol/roots.hpp"
#include "cxxmcp/protocol/sampling.hpp"
#include "cxxmcp/protocol/serialization.hpp"
#include "cxxmcp/protocol/task.hpp"

namespace fs = std::filesystem;

namespace {

using mcp::protocol::ErrorCode;
using mcp::protocol::Json;
using mcp::protocol::JsonRpcMessage;
using mcp::protocol::JsonRpcNotification;
using mcp::protocol::JsonRpcRequest;
using mcp::protocol::JsonRpcResponse;
using mcp::protocol::RequestId;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void require_error_code(const mcp::core::Result<JsonRpcMessage>& result,
                        ErrorCode expected, std::string_view context) {
  require(!result.has_value(), context);
  require(result.error().code == static_cast<int>(expected), context);
}

fs::path fixture_path(std::string_view name) {
  return fs::path(MCP_TEST_SOURCE_DIR) / "tests" / "fixtures" / "protocol" /
         std::string(name);
}

Json load_fixture_json(std::string_view name) {
  const auto path = fixture_path(name);
  std::ifstream input(path);
  require(input.is_open(),
          std::string("failed to open fixture: ") + path.string());

  Json json;
  input >> json;
  return json;
}

std::string load_fixture_text(std::string_view name) {
  const auto path = fixture_path(name);
  std::ifstream input(path);
  require(input.is_open(),
          std::string("failed to open fixture: ") + path.string());

  return std::string{std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>()};
}

void expect_serialized_json_eq(const std::string& actual, const Json& expected,
                               std::string_view context) {
  const auto parsed = Json::parse(actual);
  require(parsed == expected, context);
}

void test_initialize_request_round_trip() {
  const auto input = load_fixture_text("initialize.request.json");
  const auto parsed = mcp::protocol::parse_message(input);
  require(parsed.has_value(), "initialize request should parse");

  const auto* request = std::get_if<JsonRpcRequest>(&*parsed);
  require(request != nullptr, "initialize request should be a request");
  require(request->method == mcp::protocol::InitializeMethod,
          "initialize method mismatch");
  require(std::get<std::int64_t>(request->id) == 1, "initialize id mismatch");

  const auto serialized = mcp::protocol::serialize_message(*parsed);
  require(serialized.has_value(), "initialize request should serialize");
  expect_serialized_json_eq(*serialized,
                            load_fixture_json("initialize.request.json"),
                            "initialize round-trip mismatch");
}

void test_initialized_notification_round_trip() {
  const auto input = load_fixture_text("initialized.notification.json");
  const auto parsed = mcp::protocol::parse_message(input);
  require(parsed.has_value(), "initialized notification should parse");

  const auto* notification = std::get_if<JsonRpcNotification>(&*parsed);
  require(notification != nullptr,
          "initialized message should be a notification");
  require(notification->method == mcp::protocol::InitializedMethod,
          "initialized method mismatch");
  require(notification->method == "notifications/initialized",
          "initialized notification must use MCP method name");
  require(notification->method != "initialized",
          "initialized notification must not use legacy method name");

  const auto serialized = mcp::protocol::serialize_message(*parsed);
  require(serialized.has_value(), "initialized notification should serialize");
  expect_serialized_json_eq(*serialized,
                            load_fixture_json("initialized.notification.json"),
                            "initialized notification round-trip mismatch");

  const auto constructed = mcp::protocol::make_initialized_notification();
  require(constructed.method == "notifications/initialized",
          "constructed initialized method mismatch");
}

void test_ping_request_round_trip() {
  const auto input = load_fixture_text("ping.request.json");
  const auto parsed = mcp::protocol::parse_message(input);
  require(parsed.has_value(), "ping request should parse");

  const auto* request = std::get_if<JsonRpcRequest>(&*parsed);
  require(request != nullptr, "ping message should be a request");
  require(request->method == mcp::protocol::PingMethod, "ping method mismatch");
  require(std::get<std::string>(request->id) == "ping-1", "ping id mismatch");

  const auto serialized = mcp::protocol::serialize_message(*parsed);
  require(serialized.has_value(), "ping request should serialize");
  expect_serialized_json_eq(*serialized, load_fixture_json("ping.request.json"),
                            "ping round-trip mismatch");
}

void test_response_round_trips() {
  const auto success = mcp::protocol::make_response(
      RequestId{std::string("req-1")}, Json{{"ok", true}});
  const auto success_json =
      mcp::protocol::serialize_message(JsonRpcMessage{success});
  require(success_json.has_value(), "success response should serialize");
  expect_serialized_json_eq(*success_json,
                            load_fixture_json("response.success.json"),
                            "success response mismatch");

  const auto error = mcp::protocol::make_error_response(
      RequestId{42},
      mcp::protocol::make_error(ErrorCode::InternalError, "boom"));
  const auto error_json =
      mcp::protocol::serialize_message(JsonRpcMessage{error});
  require(error_json.has_value(), "error response should serialize");
  expect_serialized_json_eq(*error_json,
                            load_fixture_json("response.error.json"),
                            "error response mismatch");

  const auto parsed_error =
      mcp::protocol::parse_message(load_fixture_text("response.error.json"));
  require(parsed_error.has_value(), "error response should parse");
  const auto* response = std::get_if<JsonRpcResponse>(&*parsed_error);
  require(response != nullptr, "error response should be a response");
  require(response->error.has_value(),
          "error response should contain an error");
  require(response->error->code == static_cast<int>(ErrorCode::InternalError),
          "error code mismatch");
  require(response->error->message == "boom", "error message mismatch");
}

void test_tool_protocol_round_trips() {
  require(mcp::protocol::ToolsListMethod == "tools/list",
          "tools/list method mismatch");
  require(mcp::protocol::ToolsCallMethod == "tools/call",
          "tools/call method mismatch");

  const mcp::protocol::ToolsListResult list{
      .tools =
          {
              mcp::protocol::ToolDefinition{
                  .title = "Echo Tool",
                  .name = "echo",
                  .description = "Echo text",
                  .input_schema = Json{{"type", "object"}},
                  .output_schema = Json{{"type", "object"}},
                  .streaming = true,
                  .icons =
                      {
                          mcp::protocol::Icon::from_src(
                              "https://example.com/tool.png")
                              .with_mime_type("image/png")
                              .with_sizes({"48x48"})
                              .with_theme(mcp::protocol::IconTheme::Light),
                      },
                  .annotations = Json{{"beta", true}},
                  .meta = Json{{"source", "unit-test"}},
              },
          },
      .next_cursor = std::string("cursor-tools"),
  };
  const auto list_json = mcp::protocol::tools_list_result_to_json(list);
  const auto parsed_list =
      mcp::protocol::tools_list_result_from_json(list_json);
  require(parsed_list.has_value(), "tools/list result should parse");
  require(parsed_list->tools.front().title == "Echo Tool",
          "tool title mismatch");
  require(parsed_list->tools.front().name == "echo", "tool name mismatch");
  require(parsed_list->tools.front().streaming, "tool streaming mismatch");
  require(parsed_list->tools.front().output_schema.at("type") == "object",
          "tool output schema mismatch");
  require(parsed_list->tools.front().icons.size() == 1,
          "tool icon count mismatch");
  require(parsed_list->tools.front().icons.front().src ==
              "https://example.com/tool.png",
          "tool icon src mismatch");
  require(parsed_list->tools.front().icons.front().mime_type == "image/png",
          "tool icon mime type mismatch");
  require(parsed_list->tools.front().icons.front().sizes.front() == "48x48",
          "tool icon size mismatch");
  require(parsed_list->tools.front().icons.front().theme ==
              mcp::protocol::IconTheme::Light,
          "tool icon theme mismatch");
  require(parsed_list->tools.front().annotations.at("beta"),
          "tool annotations mismatch");
  require(parsed_list->tools.front().meta.has_value(), "tool meta mismatch");
  require(parsed_list->tools.front().meta->at("source") == "unit-test",
          "tool meta value mismatch");

  const mcp::protocol::ToolCall call{
      .name = "echo",
      .arguments = Json{{"value", "hello"}},
  };
  const auto call_json = mcp::protocol::tool_call_to_json(call);
  const auto parsed_call = mcp::protocol::tool_call_from_json(call_json);
  require(parsed_call.has_value(), "tools/call params should parse");
  require(parsed_call->arguments.at("value") == "hello",
          "tool call argument mismatch");

  const mcp::protocol::ToolResult result{
      .content = {mcp::protocol::ContentBlock{.type = "text", .text = "hello"}},
      .structured_content = Json{{"value", "hello"}},
  };
  const auto result_json = mcp::protocol::tool_result_to_json(result);
  const auto parsed_result = mcp::protocol::tool_result_from_json(result_json);
  require(parsed_result.has_value(), "tools/call result should parse");
  require(parsed_result->content.front().text == "hello",
          "tool result content mismatch");
  require(parsed_result->structured_content->at("value") == "hello",
          "tool structured content mismatch");
}

void test_content_block_variants_round_trip() {
  const std::vector<mcp::protocol::ContentBlock> blocks{
      [] {
        auto block = mcp::protocol::ContentBlock::text_content("hello");
        block.annotations = Json{{"audience", Json::array({"user"})}};
        block.meta = Json{{"trace", "text-1"}};
        return block;
      }(),
      mcp::protocol::ContentBlock::image("base64-image", "image/png"),
      mcp::protocol::ContentBlock::audio("base64-audio", "audio/wav"),
      mcp::protocol::ContentBlock::embedded_resource(
          mcp::protocol::ResourceContents{
              .uri = "file:///tmp/readme.txt",
              .mime_type = "text/plain",
              .text = std::string("hello resource"),
          }),
      mcp::protocol::ContentBlock::resource_link_content(
          mcp::protocol::Resource{
              .title = "Readme",
              .uri = "file:///tmp/readme.txt",
              .name = "readme.txt",
              .description = "Project readme",
              .mime_type = "text/plain",
              .size = std::int64_t{42},
          }),
  };

  require(blocks.at(0).as_text().value() == "hello",
          "text content accessor mismatch");
  require(blocks.at(1).as_image_data().value() == "base64-image",
          "image content accessor mismatch");
  require(blocks.at(2).as_audio_data().value() == "base64-audio",
          "audio content accessor mismatch");
  require(blocks.at(3).as_embedded_resource()->text.value() == "hello resource",
          "embedded resource accessor mismatch");
  require(blocks.at(4).as_resource_link()->name == "readme.txt",
          "resource link accessor mismatch");

  const auto text_json = mcp::protocol::content_block_to_json(blocks.at(0));
  require(text_json.at("text") == "hello", "text content mismatch");
  require(text_json.at("annotations").at("audience").at(0) == "user",
          "text annotations mismatch");
  require(text_json.at("_meta").at("trace") == "text-1", "text meta mismatch");

  const auto image_json = mcp::protocol::content_block_to_json(blocks.at(1));
  require(image_json.at("type") == "image", "image content type mismatch");
  require(image_json.at("data") == "base64-image",
          "image content data mismatch");
  require(image_json.at("mimeType") == "image/png",
          "image content mimeType mismatch");

  const auto audio_json = mcp::protocol::content_block_to_json(blocks.at(2));
  require(audio_json.at("type") == "audio", "audio content type mismatch");
  require(audio_json.at("data") == "base64-audio",
          "audio content data mismatch");
  require(audio_json.at("mimeType") == "audio/wav",
          "audio content mimeType mismatch");

  const auto resource_json = mcp::protocol::content_block_to_json(blocks.at(3));
  require(resource_json.at("type") == "resource",
          "embedded resource content type mismatch");
  require(resource_json.at("resource").at("uri") == "file:///tmp/readme.txt",
          "embedded resource uri mismatch");
  require(resource_json.at("resource").at("text") == "hello resource",
          "embedded resource text mismatch");

  const auto link_json = mcp::protocol::content_block_to_json(blocks.at(4));
  require(link_json.at("type") == "resource_link",
          "resource link type mismatch");
  require(link_json.at("uri") == "file:///tmp/readme.txt",
          "resource link uri mismatch");
  require(link_json.at("name") == "readme.txt", "resource link name mismatch");
  require(link_json.at("size") == 42, "resource link size mismatch");

  for (const auto& block : blocks) {
    const auto json = mcp::protocol::content_block_to_json(block);
    const auto parsed = mcp::protocol::content_block_from_json(json);
    require(parsed.has_value(), "content block variant should parse");
    require(mcp::protocol::content_block_to_json(*parsed) == json,
            "content block variant should round trip");
  }
}

void test_task_protocol_round_trips() {
  const mcp::protocol::Task task{
      .task_id = "task-1",
      .status = mcp::protocol::TaskStatus::Working,
      .status_message = std::string("working"),
      .created_at = "2026-05-24T00:00:00Z",
      .ttl = std::int64_t{600},
      .poll_interval = std::int64_t{5},
      .last_updated_at = "2026-05-24T00:00:05Z",
  };
  const auto task_json = mcp::protocol::task_to_json(task);
  const auto parsed_task = mcp::protocol::task_from_json(task_json);
  require(parsed_task.has_value(), "task should parse");
  require(parsed_task->task_id == "task-1", "task id mismatch");
  require(parsed_task->status == mcp::protocol::TaskStatus::Working,
          "task status mismatch");
  require(parsed_task->ttl.index() == 1, "task ttl mismatch");
  require(std::get<std::int64_t>(parsed_task->ttl) == 600,
          "task ttl value mismatch");

  const mcp::protocol::TaskRequestParameters request_parameters{
      .ttl = std::int64_t{300},
  };
  const auto request_json =
      mcp::protocol::task_request_parameters_to_json(request_parameters);
  const auto parsed_request =
      mcp::protocol::task_request_parameters_from_json(request_json);
  require(parsed_request.has_value(), "task request parameters should parse");
  require(parsed_request->ttl.has_value(), "task request ttl missing");
  require(parsed_request->ttl.value() == 300, "task request ttl mismatch");

  const mcp::protocol::TaskResultParams task_result_parameters{
      .task_id = "task-1",
  };
  const auto task_result_json =
      mcp::protocol::task_result_params_to_json(task_result_parameters);
  const auto parsed_task_result =
      mcp::protocol::task_result_params_from_json(task_result_json);
  require(parsed_task_result.has_value(),
          "task result parameters should parse");
  require(parsed_task_result->task_id == "task-1",
          "task result task id mismatch");

  const mcp::protocol::TaskListResult list_result{
      .tasks = {task},
      .next_cursor = std::string("cursor-task"),
  };
  const auto list_json = mcp::protocol::task_list_result_to_json(list_result);
  const auto parsed_list = mcp::protocol::task_list_result_from_json(list_json);
  require(parsed_list.has_value(), "tasks/list result should parse");
  require(parsed_list->tasks.size() == 1, "tasks/list task count mismatch");
  require(parsed_list->tasks.front().task_id == "task-1",
          "tasks/list task id mismatch");

  const mcp::protocol::CreateTaskResult create_result{
      .task = task,
      .meta = Json{{"source", "unit-test"}},
  };
  const auto create_json =
      mcp::protocol::create_task_result_to_json(create_result);
  const auto parsed_create =
      mcp::protocol::create_task_result_from_json(create_json);
  require(parsed_create.has_value(), "createTask result should parse");
  require(parsed_create->task.task_id == "task-1",
          "createTask task id mismatch");
  require(parsed_create->meta.has_value(), "createTask meta missing");
  require(parsed_create->meta->at("source") == "unit-test",
          "createTask meta mismatch");

  const mcp::protocol::JsonRpcNotification task_notification{
      .method = std::string(mcp::protocol::TasksStatusNotificationMethod),
      .params = task_json,
      .meta = Json{{"relatedTaskId", "task-1"}},
  };
  const auto serialized_notification =
      mcp::protocol::serialize_message(task_notification);
  require(serialized_notification.has_value(),
          "task notification should serialize");
  const auto parsed_notification =
      mcp::protocol::parse_message(*serialized_notification);
  require(parsed_notification.has_value(), "task notification should parse");
  const auto* parsed_task_notification =
      std::get_if<mcp::protocol::JsonRpcNotification>(&*parsed_notification);
  require(parsed_task_notification != nullptr,
          "task notification should remain a notification");
  require(parsed_task_notification->meta.has_value(),
          "task notification meta missing");
  require(parsed_task_notification->meta->at("relatedTaskId") == "task-1",
          "task notification meta mismatch");

  const mcp::protocol::ClientCapabilities client_capabilities{
      .roots = {.enabled = true, .list_changed = true},
      .sampling = {.enabled = true, .tools = true, .context = true},
      .elicitation =
          {
              .form = true,
              .form_schema_validation = true,
              .url = true,
          },
      .tasks =
          mcp::protocol::TaskCapabilities{
              .list = true,
              .cancel = true,
              .tools_call = false,
              .sampling_create_message = true,
              .elicitation_create = true,
          },
      .experimental = Json{{"beta", true}},
      .extensions = Json{{"vendor/feature", Json{{"enabled", true}}}},
  };
  const auto capabilities_json =
      mcp::protocol::client_capabilities_to_json(client_capabilities);
  require(capabilities_json.at("roots").at("listChanged") == true,
          "client roots listChanged capability mismatch");
  require(capabilities_json.at("sampling").at("tools").is_object(),
          "client sampling tools capability mismatch");
  require(capabilities_json.at("sampling").at("context").is_object(),
          "client sampling context capability mismatch");
  require(
      capabilities_json.at("elicitation").at("form").at("schemaValidation") ==
          true,
      "client elicitation schema validation capability mismatch");
  require(capabilities_json.contains("tasks"),
          "client capabilities should include tasks");
  require(capabilities_json.at("tasks").at("list").is_object(),
          "client capabilities task list should use object presence");
  require(capabilities_json.at("tasks").at("cancel").is_object(),
          "client capabilities task cancel should use object presence");
  require(capabilities_json.at("tasks")
              .at("requests")
              .at("sampling")
              .at("createMessage")
              .is_object(),
          "client capabilities sampling task should use object presence");
  require(capabilities_json.at("tasks")
              .at("requests")
              .at("elicitation")
              .at("create")
              .is_object(),
          "client capabilities elicitation task should use object presence");
  require(capabilities_json.at("experimental").at("beta"),
          "client capabilities experimental mismatch");
  require(capabilities_json.at("extensions").at("vendor/feature").at("enabled"),
          "client capabilities extensions mismatch");
  const auto parsed_capabilities =
      mcp::protocol::client_capabilities_from_json(capabilities_json);
  require(parsed_capabilities.has_value(), "client capabilities should parse");
  require(parsed_capabilities->roots.enabled,
          "parsed client roots presence mismatch");
  require(parsed_capabilities->roots.list_changed,
          "parsed client roots listChanged mismatch");
  require(parsed_capabilities->sampling.enabled,
          "parsed client sampling presence mismatch");
  require(parsed_capabilities->sampling.tools,
          "parsed client sampling tools mismatch");
  require(parsed_capabilities->sampling.context,
          "parsed client sampling context mismatch");
  require(parsed_capabilities->elicitation.form_schema_validation.has_value(),
          "parsed client elicitation schema validation missing");
  require(*parsed_capabilities->elicitation.form_schema_validation,
          "parsed client elicitation schema validation mismatch");
  require(parsed_capabilities->tasks.has_value(),
          "parsed client capabilities tasks missing");
  require(parsed_capabilities->tasks->list,
          "parsed client capabilities list mismatch");
  require(parsed_capabilities->tasks->sampling_create_message,
          "parsed client capabilities sampling task mismatch");
  require(parsed_capabilities->experimental.has_value(),
          "parsed client capabilities experimental missing");
  require(parsed_capabilities->experimental->at("beta"),
          "parsed client capabilities experimental mismatch");
  require(parsed_capabilities->extensions.at("vendor/feature").at("enabled"),
          "parsed client capabilities extension mismatch");

  Json legacy_json = Json::object();
  legacy_json["tasks"] = Json{
      {"list", true},
      {"cancel", true},
      {"requests",
       Json{
           {"tools", Json{{"call", true}}},
           {"sampling", Json{{"createMessage", true}}},
           {"elicitation", Json{{"create", true}}},
       }},
  };
  const auto legacy_capabilities =
      mcp::protocol::client_capabilities_from_json(legacy_json);
  require(legacy_capabilities.has_value(),
          "legacy boolean task capabilities should parse");
  require(legacy_capabilities->tasks.has_value(),
          "legacy task capabilities missing");
  require(legacy_capabilities->tasks->list,
          "legacy task list capability mismatch");
  require(legacy_capabilities->tasks->cancel,
          "legacy task cancel capability mismatch");
  require(legacy_capabilities->tasks->tools_call,
          "legacy task tools capability mismatch");
  require(legacy_capabilities->tasks->sampling_create_message,
          "legacy task sampling capability mismatch");
  require(legacy_capabilities->tasks->elicitation_create,
          "legacy task elicitation capability mismatch");
}

void test_client_capability_wire_shape() {
  const auto empty_capabilities = mcp::protocol::client_capabilities_to_json(
      mcp::protocol::ClientCapabilities{});
  require(empty_capabilities.empty(),
          "empty client capabilities should not advertise feature families");

  mcp::protocol::ClientCapabilities capabilities;
  capabilities.roots.enabled = true;
  capabilities.sampling.enabled = true;
  capabilities.elicitation.form = true;

  const auto json = mcp::protocol::client_capabilities_to_json(capabilities);
  require(json.at("roots").is_object(), "client roots must be an object");
  require(!json.at("roots").contains("listChanged"),
          "false client roots listChanged capability should be omitted");
  require(json.at("sampling").is_object() && json.at("sampling").empty(),
          "client sampling should use empty object presence by default");
  require(json.at("elicitation").at("form").is_object() &&
              json.at("elicitation").at("form").empty(),
          "client form elicitation should use object presence by default");
  require(!json.contains("tasks"),
          "client tasks should be omitted when not advertised");

  capabilities.tasks = mcp::protocol::TaskCapabilities{};
  const auto with_empty_tasks =
      mcp::protocol::client_capabilities_to_json(capabilities);
  require(with_empty_tasks.at("tasks").is_object() &&
              with_empty_tasks.at("tasks").empty(),
          "explicit empty client tasks capability should be preserved");
}

void test_server_capability_wire_shape() {
  const auto empty_capabilities = mcp::protocol::server_capabilities_to_json(
      mcp::protocol::ServerCapabilities{});
  require(empty_capabilities.empty(),
          "empty server capabilities should not advertise feature families");

  mcp::protocol::ServerCapabilities capabilities;
  capabilities.tools.list_changed = true;
  capabilities.resources.subscribe = true;
  capabilities.prompts.list_changed = true;
  capabilities.logging.enabled = true;
  capabilities.completions.enabled = true;
  capabilities.tasks = mcp::protocol::TaskCapabilities{
      .list = true,
      .cancel = true,
      .tools_call = true,
  };
  capabilities.experimental = Json{{"beta", true}};
  capabilities.extensions = Json{{"vendor/feature", Json::object()}};

  const auto json = mcp::protocol::server_capabilities_to_json(capabilities);
  require(json.at("tools").at("listChanged") == true,
          "server tools listChanged capability mismatch");
  require(!json.at("tools").contains("enabled"),
          "server tools capability should not use enabled marker");
  require(!json.at("resources").contains("listChanged"),
          "false resource listChanged capability should be omitted");
  require(json.at("resources").at("subscribe") == true,
          "server resources subscribe capability mismatch");
  require(json.at("prompts").at("listChanged") == true,
          "server prompts listChanged capability mismatch");
  require(json.at("logging").is_object() && json.at("logging").empty(),
          "server logging capability should use empty object presence");
  require(json.at("completions").is_object() && json.at("completions").empty(),
          "server completions capability should use empty object presence");
  require(json.at("tasks").at("list").is_object(),
          "server task list capability should use object presence");
  require(json.at("tasks").at("cancel").is_object(),
          "server task cancel capability should use object presence");
  require(json.at("tasks").at("requests").at("tools").at("call").is_object(),
          "server task tool call capability should use object presence");
  require(json.at("experimental").at("beta"),
          "server experimental capability mismatch");
  require(json.at("extensions").at("vendor/feature").is_object(),
          "server extension capability mismatch");

  mcp::protocol::ServerCapabilities presence_capabilities;
  presence_capabilities.tools.enabled = true;
  presence_capabilities.resources.enabled = true;
  presence_capabilities.prompts.enabled = true;
  presence_capabilities.tasks = mcp::protocol::TaskCapabilities{};

  const auto presence_json =
      mcp::protocol::server_capabilities_to_json(presence_capabilities);
  require(presence_json.at("tools").is_object() &&
              presence_json.at("tools").empty(),
          "explicit empty server tools capability should be preserved");
  require(presence_json.at("resources").is_object() &&
              presence_json.at("resources").empty(),
          "explicit empty server resources capability should be preserved");
  require(presence_json.at("prompts").is_object() &&
              presence_json.at("prompts").empty(),
          "explicit empty server prompts capability should be preserved");
  require(presence_json.at("tasks").is_object() &&
              presence_json.at("tasks").empty(),
          "explicit empty server tasks capability should be preserved");
}

void test_prompt_protocol_round_trips() {
  require(mcp::protocol::PromptsListMethod == "prompts/list",
          "prompts/list method mismatch");
  require(mcp::protocol::PromptsGetMethod == "prompts/get",
          "prompts/get method mismatch");

  const mcp::protocol::PromptsListResult list{
      .prompts =
          {
              mcp::protocol::Prompt{
                  .title = "Summarize Prompt",
                  .name = "summarize",
                  .description = "Summarize input",
                  .arguments =
                      {
                          mcp::protocol::PromptArgument{
                              .title = "Text Argument",
                              .name = "text",
                              .description = "Input text",
                              .required = true,
                              .annotations = Json{{"beta", true}},
                              .meta = Json{{"source", "unit-test"}},
                          },
                      },
                  .icons =
                      {
                          mcp::protocol::Icon::from_src(
                              "https://example.com/prompt.svg")
                              .with_mime_type("image/svg+xml")
                              .with_sizes({"any"})
                              .with_theme(mcp::protocol::IconTheme::Dark),
                      },
                  .annotations = Json{{"beta", true}},
                  .meta = Json{{"source", "unit-test"}},
              },
          },
      .next_cursor = std::string("cursor-1"),
  };
  const auto list_json = mcp::protocol::prompts_list_result_to_json(list);
  const auto parsed_list =
      mcp::protocol::prompts_list_result_from_json(list_json);
  require(parsed_list.has_value(), "prompts/list result should parse");
  require(parsed_list->prompts.size() == 1, "prompts/list size mismatch");
  require(parsed_list->prompts.front().title == "Summarize Prompt",
          "prompt title mismatch");
  require(parsed_list->prompts.front().name == "summarize",
          "prompt name mismatch");
  require(parsed_list->prompts.front().arguments.front().required,
          "prompt argument required mismatch");
  require(
      parsed_list->prompts.front().arguments.front().title == "Text Argument",
      "prompt argument title mismatch");
  require(parsed_list->prompts.front().arguments.front().annotations.at("beta"),
          "prompt argument annotations mismatch");
  require(parsed_list->prompts.front().arguments.front().meta.has_value(),
          "prompt argument meta missing");
  require(parsed_list->prompts.front().icons.size() == 1,
          "prompt icon count mismatch");
  require(parsed_list->prompts.front().icons.front().src ==
              "https://example.com/prompt.svg",
          "prompt icon src mismatch");
  require(parsed_list->prompts.front().icons.front().theme ==
              mcp::protocol::IconTheme::Dark,
          "prompt icon theme mismatch");
  require(parsed_list->prompts.front().annotations.at("beta"),
          "prompt annotations mismatch");
  require(parsed_list->prompts.front().meta.has_value(), "prompt meta missing");
  require(parsed_list->next_cursor == "cursor-1",
          "prompts/list cursor mismatch");
  require(mcp::protocol::prompts_list_result_to_json(*parsed_list) == list_json,
          "prompts/list round-trip mismatch");

  const mcp::protocol::PromptsGetParams params{
      .name = "summarize",
      .arguments = Json{{"text", "hello"}},
  };
  const auto params_json = mcp::protocol::prompts_get_params_to_json(params);
  const auto parsed_params =
      mcp::protocol::prompts_get_params_from_json(params_json);
  require(parsed_params.has_value(), "prompts/get params should parse");
  require(parsed_params->name == "summarize",
          "prompts/get params name mismatch");
  require(parsed_params->arguments.at("text") == "hello",
          "prompts/get params argument mismatch");

  const mcp::protocol::PromptsGetResult get{
      .description = "Summarize input",
      .messages =
          {
              mcp::protocol::PromptMessage{
                  .role = "user",
                  .content =
                      mcp::protocol::ContentBlock{.type = "text",
                                                  .text = "Summarize {{text}}"},
              },
          },
  };
  const auto get_json = mcp::protocol::prompts_get_result_to_json(get);
  const auto parsed_get = mcp::protocol::prompts_get_result_from_json(get_json);
  require(parsed_get.has_value(), "prompts/get result should parse");
  require(parsed_get->messages.size() == 1,
          "prompts/get message size mismatch");
  require(parsed_get->messages.front().role == "user",
          "prompts/get role mismatch");
  require(parsed_get->messages.front().content.text == "Summarize {{text}}",
          "prompts/get content mismatch");
  require(mcp::protocol::prompts_get_result_to_json(*parsed_get) == get_json,
          "prompts/get result round-trip mismatch");
}

void test_resource_protocol_round_trips() {
  require(mcp::protocol::ResourcesListMethod == "resources/list",
          "resources/list method mismatch");
  require(mcp::protocol::ResourcesReadMethod == "resources/read",
          "resources/read method mismatch");
  require(
      mcp::protocol::ResourcesTemplatesListMethod == "resources/templates/list",
      "resources/templates/list method mismatch");
  require(mcp::protocol::ResourcesSubscribeMethod == "resources/subscribe",
          "resources/subscribe method mismatch");
  require(mcp::protocol::ResourcesUnsubscribeMethod == "resources/unsubscribe",
          "resources/unsubscribe method mismatch");

  const mcp::protocol::ResourcesListResult list{
      .resources =
          {
              mcp::protocol::Resource{
                  .title = "Readme",
                  .uri = "file:///tmp/readme.txt",
                  .name = "readme",
                  .description = "Readme",
                  .mime_type = "text/plain",
                  .size = std::int64_t{42},
                  .icons =
                      {
                          mcp::protocol::Icon::from_src(
                              "https://example.com/resource.png")
                              .with_mime_type("image/png")
                              .with_sizes({"32x32"})
                              .with_theme(mcp::protocol::IconTheme::Light),
                      },
                  .annotations = Json{{"beta", true}},
                  .meta = Json{{"source", "unit-test"}},
              },
          },
      .next_cursor = std::string("cursor-2"),
  };
  const auto list_json = mcp::protocol::resources_list_result_to_json(list);
  const auto parsed_list =
      mcp::protocol::resources_list_result_from_json(list_json);
  require(parsed_list.has_value(), "resources/list result should parse");
  require(parsed_list->resources.size() == 1, "resources/list size mismatch");
  require(parsed_list->resources.front().title == "Readme",
          "resource title mismatch");
  require(parsed_list->resources.front().uri == "file:///tmp/readme.txt",
          "resource uri mismatch");
  require(parsed_list->resources.front().mime_type == "text/plain",
          "resource mime type mismatch");
  require(parsed_list->resources.front().size == 42, "resource size mismatch");
  require(parsed_list->resources.front().icons.size() == 1,
          "resource icon count mismatch");
  require(parsed_list->resources.front().icons.front().src ==
              "https://example.com/resource.png",
          "resource icon src mismatch");
  require(parsed_list->resources.front().annotations.at("beta"),
          "resource annotations mismatch");
  require(parsed_list->resources.front().meta.has_value(),
          "resource meta missing");
  require(parsed_list->next_cursor == "cursor-2",
          "resources/list cursor mismatch");
  require(
      mcp::protocol::resources_list_result_to_json(*parsed_list) == list_json,
      "resources/list round-trip mismatch");

  const mcp::protocol::ResourcesReadParams params{.uri =
                                                      "file:///tmp/readme.txt"};
  const auto params_json = mcp::protocol::resources_read_params_to_json(params);
  const auto parsed_params =
      mcp::protocol::resources_read_params_from_json(params_json);
  require(parsed_params.has_value(), "resources/read params should parse");
  require(parsed_params->uri == "file:///tmp/readme.txt",
          "resources/read params uri mismatch");

  const mcp::protocol::ResourcesReadResult read{
      .contents =
          {
              mcp::protocol::ResourceContents{
                  .uri = "file:///tmp/readme.txt",
                  .mime_type = "text/plain",
                  .text = std::string("hello"),
              },
          },
  };
  const auto read_json = mcp::protocol::resources_read_result_to_json(read);
  const auto parsed_read =
      mcp::protocol::resources_read_result_from_json(read_json);
  require(parsed_read.has_value(), "resources/read result should parse");
  require(parsed_read->contents.size() == 1,
          "resources/read contents size mismatch");
  require(parsed_read->contents.front().text == "hello",
          "resources/read text mismatch");
  require(
      mcp::protocol::resources_read_result_to_json(*parsed_read) == read_json,
      "resources/read round-trip mismatch");

  const mcp::protocol::ResourceTemplatesListResult templates{
      .resource_templates =
          {
              mcp::protocol::ResourceTemplate{
                  .uri_template = "file:///tmp/{name}.txt",
                  .name = "tmp file",
                  .description = "Templated tmp file",
                  .mime_type = "text/plain",
                  .icons =
                      {
                          mcp::protocol::Icon::from_src(
                              "https://example.com/template.png")
                              .with_mime_type("image/png")
                              .with_sizes({"16x16"}),
                      },
              },
          },
      .next_cursor = std::string("cursor-3"),
  };
  const auto templates_json =
      mcp::protocol::resource_templates_list_result_to_json(templates);
  const auto parsed_templates =
      mcp::protocol::resource_templates_list_result_from_json(templates_json);
  require(parsed_templates.has_value(),
          "resources/templates/list result should parse");
  require(parsed_templates->resource_templates.size() == 1,
          "resource template size mismatch");
  require(parsed_templates->resource_templates.front().uri_template ==
              "file:///tmp/{name}.txt",
          "resource template uriTemplate mismatch");
  require(parsed_templates->resource_templates.front().icons.size() == 1,
          "resource template icon count mismatch");
  require(parsed_templates->resource_templates.front().icons.front().src ==
              "https://example.com/template.png",
          "resource template icon src mismatch");
  require(mcp::protocol::resource_templates_list_result_to_json(
              *parsed_templates) == templates_json,
          "resources/templates/list round-trip mismatch");

  const mcp::protocol::ResourcesSubscribeParams subscribe{
      .uri = "file:///tmp/readme.txt"};
  const auto subscribe_json =
      mcp::protocol::resources_subscribe_params_to_json(subscribe);
  const auto parsed_subscribe =
      mcp::protocol::resources_subscribe_params_from_json(subscribe_json);
  require(parsed_subscribe.has_value(),
          "resources/subscribe params should parse");
  require(parsed_subscribe->uri == "file:///tmp/readme.txt",
          "resources/subscribe uri mismatch");
}

void test_roots_completion_logging_sampling_round_trips() {
  require(mcp::protocol::RootsListMethod == "roots/list",
          "roots/list method mismatch");
  require(mcp::protocol::CompletionCompleteMethod == "completion/complete",
          "completion/complete method mismatch");
  require(mcp::protocol::LoggingSetLevelMethod == "logging/setLevel",
          "logging/setLevel method mismatch");
  require(
      mcp::protocol::SamplingCreateMessageMethod == "sampling/createMessage",
      "sampling/createMessage method mismatch");

  const mcp::protocol::RootsListResult roots{
      .roots = {mcp::protocol::Root{.uri = "file:///workspace",
                                    .name = "workspace"}},
  };
  const auto roots_json = mcp::protocol::roots_list_result_to_json(roots);
  const auto parsed_roots =
      mcp::protocol::roots_list_result_from_json(roots_json);
  require(parsed_roots.has_value(), "roots/list result should parse");
  require(parsed_roots->roots.front().name == "workspace",
          "root name mismatch");

  const mcp::protocol::CompleteParams complete{
      .ref = mcp::protocol::CompletionReference{.type = "ref/prompt",
                                                .name = "summarize"},
      .argument =
          mcp::protocol::CompletionArgument{.name = "text", .value = "he"},
      .context = Json{{"arguments", Json{{"other", "value"}}}},
  };
  const auto complete_json = mcp::protocol::complete_params_to_json(complete);
  const auto parsed_complete =
      mcp::protocol::complete_params_from_json(complete_json);
  require(parsed_complete.has_value(), "completion params should parse");
  require(parsed_complete->argument.value == "he",
          "completion argument mismatch");

  const mcp::protocol::CompleteResult complete_result{
      .completion =
          mcp::protocol::CompletionResult{
              .values = {"hello", "help"},
              .total = 2,
              .has_more = false,
          },
  };
  const auto complete_result_json =
      mcp::protocol::complete_result_to_json(complete_result);
  const auto parsed_complete_result =
      mcp::protocol::complete_result_from_json(complete_result_json);
  require(parsed_complete_result.has_value(), "completion result should parse");
  require(parsed_complete_result->completion.values.size() == 2,
          "completion values mismatch");

  const mcp::protocol::LoggingSetLevelParams log_level{
      .level = mcp::protocol::LoggingLevel::Warning};
  const auto log_level_json =
      mcp::protocol::logging_set_level_params_to_json(log_level);
  const auto parsed_log_level =
      mcp::protocol::logging_set_level_params_from_json(log_level_json);
  require(parsed_log_level.has_value(), "logging/setLevel params should parse");
  require(parsed_log_level->level == mcp::protocol::LoggingLevel::Warning,
          "logging level mismatch");

  const mcp::protocol::CreateMessageParams sample{
      .messages =
          {
              mcp::protocol::SamplingMessage{
                  .role = "user",
                  .content = mcp::protocol::ContentBlock{.type = "text",
                                                         .text = "hello"},
              },
          },
      .model_preferences =
          mcp::protocol::ModelPreferences{
              .hints = {mcp::protocol::ModelHint{.name = "fast-model"}},
              .speed_priority = 0.8,
          },
      .system_prompt = std::string("be concise"),
      .include_context = std::string("thisServer"),
      .temperature = 0.2,
      .max_tokens = 128,
      .stop_sequences = {"stop"},
      .metadata = Json{{"trace", "abc"}},
  };
  const auto sample_json = mcp::protocol::create_message_params_to_json(sample);
  const auto parsed_sample =
      mcp::protocol::create_message_params_from_json(sample_json);
  require(parsed_sample.has_value(),
          "sampling/createMessage params should parse");
  require(parsed_sample->messages.front().content.text == "hello",
          "sampling message mismatch");
  require(parsed_sample->model_preferences->hints.front().name == "fast-model",
          "sampling model hint mismatch");

  const mcp::protocol::CreateMessageResult sample_result{
      .role = "assistant",
      .content = mcp::protocol::ContentBlock{.type = "text", .text = "hi"},
      .model = "fast-model",
      .stop_reason = "endTurn",
  };
  const auto sample_result_json =
      mcp::protocol::create_message_result_to_json(sample_result);
  const auto parsed_sample_result =
      mcp::protocol::create_message_result_from_json(sample_result_json);
  require(parsed_sample_result.has_value(),
          "sampling/createMessage result should parse");
  require(parsed_sample_result->model == "fast-model",
          "sampling result model mismatch");
}

void test_elicitation_protocol_round_trips() {
  require(mcp::protocol::ElicitationCreateMethod == "elicitation/create",
          "elicitation/create method mismatch");
  require(mcp::protocol::ElicitationCompleteNotificationMethod ==
              "notifications/elicitation/complete",
          "elicitation completion notification method mismatch");

  const auto schema = mcp::protocol::ElicitationSchema::Builder()
                          .title("Choose response")
                          .description("Pick one value")
                          .required_email("email")
                          .optional_bool("remember", true)
                          .build();
  require(schema.has_value(), "elicitation schema build failed");
  require(schema->properties.size() == 2,
          "elicitation schema property count mismatch");
  require(schema->required.size() == 1, "elicitation required count mismatch");
  require(schema->required.front() == "email",
          "elicitation required field mismatch");

  const mcp::protocol::CreateElicitationRequestParam request{
      .message = "Choose a response",
      .requested_schema = *schema,
  };
  const auto request_json =
      mcp::protocol::create_elicitation_request_param_to_json(request);
  const auto parsed_request =
      mcp::protocol::create_elicitation_request_param_from_json(request_json);
  require(parsed_request.has_value(), "elicitation request should parse");
  require(parsed_request->message == "Choose a response",
          "elicitation request message mismatch");
  require(parsed_request->requested_schema.properties.count("email") == 1,
          "elicitation request schema property mismatch");

  const mcp::protocol::CreateElicitationResult result{
      .action = mcp::protocol::ElicitationAction::Accept,
      .content = Json{{"value", "accepted"}},
  };
  const auto result_json =
      mcp::protocol::create_elicitation_result_to_json(result);
  const auto parsed_result =
      mcp::protocol::create_elicitation_result_from_json(result_json);
  require(parsed_result.has_value(), "elicitation result should parse");
  require(parsed_result->action == mcp::protocol::ElicitationAction::Accept,
          "elicitation result action mismatch");
  require(parsed_result->content.has_value(),
          "elicitation result content missing");
  require(parsed_result->content->at("value") == "accepted",
          "elicitation result content mismatch");

  mcp::protocol::CreateElicitationRequestParam url_request;
  url_request.message = "Open the verification link";
  url_request.mode = mcp::protocol::ElicitationMode::Url;
  url_request.elicitation_id = "elicitation-1";
  url_request.url = "https://example.test/elicitation/1";
  url_request.request_state = Json{{"stage", 2}};
  const auto url_request_json =
      mcp::protocol::create_elicitation_request_param_to_json(url_request);
  const auto parsed_url_request =
      mcp::protocol::create_elicitation_request_param_from_json(
          url_request_json);
  require(parsed_url_request.has_value(),
          "url elicitation request should parse");
  require(parsed_url_request->mode == mcp::protocol::ElicitationMode::Url,
          "url elicitation request mode mismatch");
  require(parsed_url_request->elicitation_id.has_value(),
          "url elicitation id missing");
  require(parsed_url_request->elicitation_id.value() == "elicitation-1",
          "url elicitation id mismatch");
  require(parsed_url_request->url.has_value(), "url elicitation url missing");
  require(
      parsed_url_request->url.value() == "https://example.test/elicitation/1",
      "url elicitation url mismatch");
  require(parsed_url_request->request_state.has_value(),
          "url elicitation request state missing");
  require(parsed_url_request->request_state->at("stage") == 2,
          "url elicitation request state mismatch");

  const mcp::protocol::ElicitationCompleteNotificationParams completion_params{
      .elicitation_id = "elicitation-1",
  };
  const auto completion_json =
      mcp::protocol::elicitation_complete_notification_params_to_json(
          completion_params);
  const auto parsed_completion =
      mcp::protocol::elicitation_complete_notification_params_from_json(
          completion_json);
  require(parsed_completion.has_value(),
          "elicitation completion notification should parse");
  require(parsed_completion->elicitation_id == "elicitation-1",
          "elicitation completion id mismatch");
}

void test_notification_helpers_round_trip() {
  require(
      mcp::protocol::CancelledNotificationMethod == "notifications/cancelled",
      "cancelled notification method mismatch");
  require(mcp::protocol::ProgressNotificationMethod == "notifications/progress",
          "progress notification method mismatch");
  require(mcp::protocol::RootsListChangedNotificationMethod ==
              "notifications/roots/list_changed",
          "roots list changed notification method mismatch");
  require(mcp::protocol::ElicitationCompleteNotificationMethod ==
              "notifications/elicitation/complete",
          "elicitation completion notification method mismatch");
  require(mcp::protocol::ResourcesUpdatedNotificationMethod ==
              "notifications/resources/updated",
          "resources updated notification method mismatch");
  require(mcp::protocol::LoggingMessageNotificationMethod ==
              "notifications/message",
          "logging message notification method mismatch");

  const mcp::protocol::CancelledNotificationParams cancelled{
      .request_id = RequestId{std::string("request-1")},
      .reason = "no longer needed",
  };
  const auto cancelled_json =
      mcp::protocol::cancelled_notification_params_to_json(cancelled);
  const auto parsed_cancelled =
      mcp::protocol::cancelled_notification_params_from_json(cancelled_json);
  require(parsed_cancelled.has_value(),
          "cancelled notification params should parse");
  require(std::get<std::string>(parsed_cancelled->request_id) == "request-1",
          "cancelled request id mismatch");

  const mcp::protocol::ProgressNotificationParams progress{
      .progress_token = mcp::protocol::ProgressToken{std::string("token-1")},
      .progress = 2.0,
      .total = 4.0,
      .message = "halfway",
  };
  const auto progress_json =
      mcp::protocol::progress_notification_params_to_json(progress);
  const auto parsed_progress =
      mcp::protocol::progress_notification_params_from_json(progress_json);
  require(parsed_progress.has_value(),
          "progress notification params should parse");
  require(std::get<std::string>(parsed_progress->progress_token) == "token-1",
          "progress token mismatch");
  require(parsed_progress->total == 4.0, "progress total mismatch");

  const mcp::protocol::ElicitationCompleteNotificationParams completion{
      .elicitation_id = "elicitation-1",
  };
  const auto completion_json =
      mcp::protocol::elicitation_complete_notification_params_to_json(
          completion);
  const auto parsed_completion =
      mcp::protocol::elicitation_complete_notification_params_from_json(
          completion_json);
  require(parsed_completion.has_value(),
          "elicitation completion params should parse");
  require(parsed_completion->elicitation_id == "elicitation-1",
          "elicitation completion id mismatch");

  const mcp::protocol::LoggingMessageNotificationParams log_message{
      .level = mcp::protocol::LoggingLevel::Info,
      .logger = "test",
      .data = Json{{"message", "hello"}},
  };
  const auto log_message_json =
      mcp::protocol::logging_message_notification_params_to_json(log_message);
  const auto parsed_log_message =
      mcp::protocol::logging_message_notification_params_from_json(
          log_message_json);
  require(parsed_log_message.has_value(),
          "logging message notification params should parse");
  require(parsed_log_message->logger == "test",
          "logging message logger mismatch");

  const auto notification = mcp::protocol::make_notification(
      std::string(mcp::protocol::ProgressNotificationMethod), progress_json);
  require(notification.method == "notifications/progress",
          "constructed progress notification method mismatch");
}

void test_invalid_json_is_rejected() {
  const auto parsed = mcp::protocol::parse_message("{not json");
  require_error_code(parsed, ErrorCode::ParseError,
                     "invalid JSON should be rejected");
}

void test_invalid_messages_are_rejected() {
  const auto missing_method = mcp::protocol::parse_message(
      load_fixture_text("invalid.missing_method.json"));
  require_error_code(missing_method, ErrorCode::InvalidRequest,
                     "missing method should be rejected");

  const auto version =
      mcp::protocol::parse_message(load_fixture_text("invalid.version.json"));
  require_error_code(version, ErrorCode::InvalidRequest,
                     "bad version should be rejected");

  const auto null_id = mcp::protocol::parse_message(
      load_fixture_text("invalid.null_request_id.json"));
  require_error_code(null_id, ErrorCode::InvalidRequest,
                     "null request id should be rejected");

  const auto missing_payload = mcp::protocol::parse_message(
      load_fixture_text("invalid.response_missing_payload.json"));
  require_error_code(missing_payload, ErrorCode::InvalidRequest,
                     "response without payload should be rejected");

  const auto dual_payload = mcp::protocol::parse_message(
      load_fixture_text("invalid.dual_result_error.json"));
  require_error_code(dual_payload, ErrorCode::InvalidRequest,
                     "response with both payloads should be rejected");
}

}  // namespace

int main() {
  const std::vector<std::pair<std::string_view, void (*)()>> tests = {
      {"initialize request round trip", test_initialize_request_round_trip},
      {"initialized notification round trip",
       test_initialized_notification_round_trip},
      {"ping request round trip", test_ping_request_round_trip},
      {"response round trips", test_response_round_trips},
      {"tool protocol round trips", test_tool_protocol_round_trips},
      {"content block variants round trip",
       test_content_block_variants_round_trip},
      {"task protocol round trips", test_task_protocol_round_trips},
      {"client capability wire shape", test_client_capability_wire_shape},
      {"server capability wire shape", test_server_capability_wire_shape},
      {"prompt protocol round trips", test_prompt_protocol_round_trips},
      {"resource protocol round trips", test_resource_protocol_round_trips},
      {"roots completion logging sampling round trips",
       test_roots_completion_logging_sampling_round_trips},
      {"elicitation protocol round trips",
       test_elicitation_protocol_round_trips},
      {"notification helpers round trip", test_notification_helpers_round_trip},
      {"invalid json is rejected", test_invalid_json_is_rejected},
      {"invalid messages are rejected", test_invalid_messages_are_rejected},
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
