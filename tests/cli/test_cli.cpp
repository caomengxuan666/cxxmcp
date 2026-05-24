#include "cxxmcp/app/server_management.hpp"
#include "cxxmcp/app/services.hpp"
#include "cxxmcp/cli/commands.hpp"
#include "cxxmcp/cli/runtime.hpp"
#include "cxxmcp/client/client.hpp"
#include "cxxmcp/client/http_transport.hpp"
#include "cxxmcp/protocol/serialization.hpp"

#include "httplib.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace {

    using mcp::protocol::Json;

    void require(bool condition, std::string_view message) {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    mcp::app::Policy approved_policy(bool enabled) {
        mcp::app::Policy policy;
        policy.approval = mcp::app::ApprovalState::approved;
        policy.enabled = enabled;
        policy.permissions.insert(mcp::app::Permission::filesystem_read);
        return policy;
    }

    mcp::app::ToolDescriptor tool(std::string id, std::string name, bool enabled, std::string profile_id = "default") {
        return mcp::app::ToolDescriptor{
                .id = std::move(id),
                .definition = mcp::protocol::ToolDefinition{
                        .name = std::move(name),
                        .description = "Test tool",
                        .input_schema = Json::object(),
                        .streaming = false,
                },
                .source = mcp::app::ToolSource{
                        .kind = mcp::app::ToolSourceKind::local_manifest,
                        .location = "tools/test.json",
                },
                .policy = approved_policy(enabled),
                .profile_id = std::move(profile_id),
        };
    }

    mcp::app::Profile profile(std::string id = "default", std::string name = "Default") {
        return mcp::app::Profile{
                .id = std::move(id),
                .name = std::move(name),
                .endpoints = {mcp::app::Endpoint{.name = "stdio", .url = "stdio://local"}},
                .enabled_tool_ids = {"tool.echo"},
                .environment = {},
        };
    }

    mcp::app::McpServerDefinition mcp_server(std::string id = "filesystem", std::string name = "filesystem") {
        return mcp::app::McpServerDefinition{
                .id = std::move(id),
                .name = std::move(name),
                .display_name = "Filesystem",
                .description = "Test cxxmcp server",
                .transport = mcp::app::McpServerTransportKind::stdio,
                .stdio = mcp::app::StdioLaunchConfig{.command = "node", .args = {"server.js"}},
                .enabled = true,
                .auto_start = true,
                .trust = mcp::app::McpServerTrustState::trusted,
        };
    }

    mcp::app::DiscoveredCapability capability(std::string id,
                                              mcp::app::CapabilityKind kind,
                                              std::string upstream_name,
                                              std::string exposed_name) {
        return mcp::app::DiscoveredCapability{
                .id = std::move(id),
                .kind = kind,
                .server_id = "filesystem",
                .upstream_name = std::move(upstream_name),
                .exposed_name = std::move(exposed_name),
                .title = "Test capability",
                .description = "Test capability description",
        };
    }

    class FakeDiscoverySession final : public mcp::app::McpDiscoverySession {
    public:
        mcp::core::Result<mcp::core::Unit> initialize() override {
            initialized = true;
            return mcp::core::Unit{};
        }

        mcp::core::Result<std::vector<mcp::protocol::ToolDefinition>> discover_tools() override {
            require(initialized, "discovery session should initialize before listing tools");
            return std::vector<mcp::protocol::ToolDefinition>{
                    mcp::protocol::ToolDefinition{
                            .name = "read_file",
                            .description = "Read a file",
                            .input_schema = Json{{"type", "object"}},
                    },
            };
        }

        mcp::core::Result<std::vector<mcp::protocol::Prompt>> discover_prompts() override {
            require(initialized, "discovery session should initialize before listing prompts");
            return std::vector<mcp::protocol::Prompt>{
                    mcp::protocol::Prompt{
                            .name = "summarize",
                            .description = "Summarize text",
                            .arguments = {mcp::protocol::PromptArgument{.name = "text", .required = true}},
                    },
            };
        }

        mcp::core::Result<std::vector<mcp::protocol::Resource>> discover_resources() override {
            require(initialized, "discovery session should initialize before listing resources");
            return std::vector<mcp::protocol::Resource>{
                    mcp::protocol::Resource{
                            .uri = "file:///tmp/readme.md",
                            .name = "readme",
                            .description = "Readme file",
                            .mime_type = "text/markdown",
                    },
            };
        }

    private:
        bool initialized = false;
    };

    class FailingInitializeDiscoverySession final : public mcp::app::McpDiscoverySession {
    public:
        mcp::core::Result<mcp::core::Unit> initialize() override {
            return std::unexpected(mcp::core::Error{1, "upstream initialize failed", "test failure"});
        }

        mcp::core::Result<std::vector<mcp::protocol::ToolDefinition>> discover_tools() override {
            return std::vector<mcp::protocol::ToolDefinition>{};
        }

        mcp::core::Result<std::vector<mcp::protocol::Prompt>> discover_prompts() override {
            return std::vector<mcp::protocol::Prompt>{};
        }

        mcp::core::Result<std::vector<mcp::protocol::Resource>> discover_resources() override {
            return std::vector<mcp::protocol::Resource>{};
        }
    };

    mcp::app::McpDiscoverySessionFactory fake_discovery_factory() {
        return [](const mcp::app::McpServerDefinition &) -> mcp::core::Result<std::unique_ptr<mcp::app::McpDiscoverySession>> {
            return std::make_unique<FakeDiscoverySession>();
        };
    }

    mcp::app::McpDiscoverySessionFactory failing_initialize_discovery_factory() {
        return [](const mcp::app::McpServerDefinition &)
                       -> mcp::core::Result<std::unique_ptr<mcp::app::McpDiscoverySession>> {
            return std::make_unique<FailingInitializeDiscoverySession>();
        };
    }

    class CliHarness {
    public:
        explicit CliHarness(std::vector<mcp::app::ToolDescriptor> tools = {tool("tool.echo", "echo", true)},
                            std::vector<mcp::app::Profile> profiles = {profile()},
                            bool json_output = false,
                            mcp::app::McpDiscoverySessionFactory discovery_factory = fake_discovery_factory())
            : tools_(std::move(tools)),
              profiles_(std::move(profiles)),
              servers_(),
              capabilities_(),
              exposure_profiles_(),
              management_(tools_, profiles_),
              server_management_(servers_, capabilities_, exposure_profiles_, std::move(discovery_factory)),
              exposure_management_(exposure_profiles_, capabilities_),
              cli_(mcp::cli::CommandServices{.management = management_, .tools = tools_, .profiles = profiles_, .bundles = bundles_, .servers = servers_, .capabilities = capabilities_, .exposure_profiles = exposure_profiles_, .server_management = server_management_, .exposure_management = exposure_management_, .executable_path = "C:/bin/cxxmcp.exe", .state_directory = "C:/mcp-state", .json_output = json_output}) {}

        int run(std::initializer_list<std::string_view> args) {
            std::vector<std::string_view> values(args);
            const auto result = cli_.run(values, out, err);
            require(result.has_value(), "cli run returned unexpected error");
            return *result;
        }

        int run_with_input(std::initializer_list<std::string_view> args, std::istream &in) {
            std::vector<std::string_view> values(args);
            const auto result = cli_.run(values, in, out, err);
            require(result.has_value(), "cli run returned unexpected error");
            return *result;
        }

        mcp::app::MemoryToolCatalog tools_;
        mcp::app::MemoryProfileStore profiles_;
        mcp::app::MemoryMcpServerStore servers_;
        mcp::app::MemoryCapabilityCatalog capabilities_;
        mcp::app::MemoryExposureProfileStore exposure_profiles_;
        mcp::app::ToolManagementService management_;
        mcp::app::ServerManagementService server_management_;
        mcp::app::ExposureManagementService exposure_management_;
        mcp::app::JsonImportExportService bundles_;
        mcp::cli::CommandApp cli_;
        std::ostringstream out;
        std::ostringstream err;
    };

#ifdef _WIN32
    std::wstring quote_wide(const std::filesystem::path &path) {
        return L"\"" + path.wstring() + L"\"";
    }

    std::string normalize_newlines(std::string text) {
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        return text;
    }

    std::uint16_t reserve_free_port() {
        WSADATA startup{};
        require(WSAStartup(MAKEWORD(2, 2), &startup) == 0, "WSAStartup should succeed");
        struct WsaCleanup {
            ~WsaCleanup() {
                WSACleanup();
            }
        } wsa_cleanup{};

        SOCKET socket_handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        require(socket_handle != INVALID_SOCKET, "socket creation should succeed");

        struct SocketCleanup {
            SOCKET handle;
            ~SocketCleanup() {
                if (handle != INVALID_SOCKET) {
                    closesocket(handle);
                }
            }
        } cleanup{socket_handle};

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;

        require(::bind(socket_handle, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != SOCKET_ERROR,
                "ephemeral port bind should succeed");

        int address_length = sizeof(address);
        require(::getsockname(socket_handle, reinterpret_cast<sockaddr *>(&address), &address_length) != SOCKET_ERROR,
                "ephemeral port lookup should succeed");

        return ntohs(address.sin_port);
    }

    std::wstring make_command_line(const std::vector<std::wstring> &parts) {
        std::wstring command_line;
        for (std::size_t index = 0; index < parts.size(); ++index) {
            if (index != 0) {
                command_line.push_back(L' ');
            }
            command_line += parts[index];
        }
        return command_line;
    }

    mcp::core::Result<PROCESS_INFORMATION> start_process(const std::wstring &command_line) {
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};

        std::vector<wchar_t> buffer(command_line.begin(), command_line.end());
        buffer.push_back(L'\0');
        if (!CreateProcessW(nullptr,
                            buffer.data(),
                            nullptr,
                            nullptr,
                            FALSE,
                            CREATE_NO_WINDOW,
                            nullptr,
                            nullptr,
                            &startup,
                            &process)) {
            return std::unexpected(mcp::core::Error{1, "failed to launch cli process"});
        }

        return process;
    }

    int wait_process(PROCESS_INFORMATION &process) {
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD exit_code = 1;
        if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
            return 1;
        }
        return static_cast<int>(exit_code);
    }

    void close_process(PROCESS_INFORMATION &process) {
        if (process.hThread != nullptr) {
            CloseHandle(process.hThread);
            process.hThread = nullptr;
        }
        if (process.hProcess != nullptr) {
            CloseHandle(process.hProcess);
            process.hProcess = nullptr;
        }
    }

    mcp::core::Result<std::pair<int, std::string>> run_process_capture(
            const std::wstring &command_line,
            std::optional<std::string_view> input_text = std::nullopt) {
        const auto output_path = std::filesystem::temp_directory_path() /
                                 ("mcp-cli-capture-" + std::to_string(GetCurrentProcessId()) + "-" +
                                  std::to_string(GetTickCount64()) + ".txt");

        SECURITY_ATTRIBUTES security_attributes{};
        security_attributes.nLength = sizeof(security_attributes);
        security_attributes.lpSecurityDescriptor = nullptr;
        security_attributes.bInheritHandle = TRUE;

        HANDLE output_handle = CreateFileW(output_path.c_str(),
                                           GENERIC_WRITE,
                                           FILE_SHARE_READ,
                                           &security_attributes,
                                           CREATE_ALWAYS,
                                           FILE_ATTRIBUTE_TEMPORARY,
                                           nullptr);
        if (output_handle == INVALID_HANDLE_VALUE) {
            return std::unexpected(mcp::core::Error{1, "failed to create capture file"});
        }

        HANDLE input_read = INVALID_HANDLE_VALUE;
        HANDLE input_write = INVALID_HANDLE_VALUE;
        if (input_text.has_value()) {
            if (!CreatePipe(&input_read, &input_write, &security_attributes, 0)) {
                CloseHandle(output_handle);
                return std::unexpected(mcp::core::Error{1, "failed to create input pipe"});
            }
            SetHandleInformation(input_write, HANDLE_FLAG_INHERIT, 0);
        }

        struct OutputCleanup {
            HANDLE handle;
            std::filesystem::path path;
            ~OutputCleanup() {
                if (handle != INVALID_HANDLE_VALUE) {
                    CloseHandle(handle);
                }
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        } cleanup{output_handle, output_path};
        struct InputCleanup {
            HANDLE &read;
            HANDLE &write;
            ~InputCleanup() {
                if (read != INVALID_HANDLE_VALUE) {
                    CloseHandle(read);
                    read = INVALID_HANDLE_VALUE;
                }
                if (write != INVALID_HANDLE_VALUE) {
                    CloseHandle(write);
                    write = INVALID_HANDLE_VALUE;
                }
            }
        } input_cleanup{input_read, input_write};

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = input_text.has_value() ? input_read : GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = output_handle;
        startup.hStdError = output_handle;

        PROCESS_INFORMATION process{};
        std::vector<wchar_t> buffer(command_line.begin(), command_line.end());
        buffer.push_back(L'\0');
        if (!CreateProcessW(nullptr,
                            buffer.data(),
                            nullptr,
                            nullptr,
                            TRUE,
                            CREATE_NO_WINDOW,
                            nullptr,
                            nullptr,
                            &startup,
                            &process)) {
            return std::unexpected(mcp::core::Error{1, "failed to launch cli process"});
        }
        if (input_read != INVALID_HANDLE_VALUE) {
            CloseHandle(input_read);
            input_read = INVALID_HANDLE_VALUE;
        }

        struct ProcessCleanup {
            PROCESS_INFORMATION &process;
            ~ProcessCleanup() {
                close_process(process);
            }
        } process_cleanup{process};

        if (input_text.has_value()) {
            std::string_view remaining = *input_text;
            while (!remaining.empty()) {
                const auto chunk_size = static_cast<DWORD>(std::min<std::size_t>(remaining.size(), 4096));
                DWORD written = 0;
                if (!WriteFile(input_write, remaining.data(), chunk_size, &written, nullptr)) {
                    return std::unexpected(mcp::core::Error{1, "failed to write cli process input"});
                }
                remaining.remove_prefix(written);
            }
            CloseHandle(input_write);
            input_write = INVALID_HANDLE_VALUE;
        }

        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD exit_code = 1;
        if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
            return std::unexpected(mcp::core::Error{1, "failed to read cli process exit code"});
        }

        cleanup.handle = INVALID_HANDLE_VALUE;
        CloseHandle(output_handle);

        std::ifstream input(output_path, std::ios::binary);
        std::ostringstream captured;
        captured << input.rdbuf();

        return std::make_pair(static_cast<int>(exit_code), normalize_newlines(captured.str()));
    }
#endif

    bool wait_for_http_gateway(int port, std::string_view path) {
        for (int attempt = 0; attempt < 80; ++attempt) {
            mcp::client::Client client(std::make_unique<mcp::client::HttpTransport>(
                    mcp::client::HttpTransportOptions{
                            .host = "127.0.0.1",
                            .port = port,
                            .path = std::string(path),
                            .headers = {},
                            .timeout = std::chrono::milliseconds{100},
                    }));

            const auto response = client.raw_request(mcp::protocol::make_ping_request(std::int64_t{1}));
            if (response.has_value()) {
                return true;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
        return false;
    }

    class HttpUpstreamMcpFixture {
    public:
        HttpUpstreamMcpFixture() {
            server_.Post("/mcp", [this](const httplib::Request &request, httplib::Response &response) {
                handle(request, response);
            });
            port_ = server_.bind_to_any_port("127.0.0.1");
            if (port_ <= 0) {
                throw std::runtime_error("failed to bind http upstream fixture");
            }
            thread_ = std::thread([this]() {
                server_.listen_after_bind();
            });
            server_.wait_until_ready();
        }

        ~HttpUpstreamMcpFixture() {
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
        void set_response(httplib::Response &response, const mcp::protocol::JsonRpcResponse &rpc_response) {
            const auto serialized = mcp::protocol::serialize_response(rpc_response);
            require(serialized.has_value(), "http upstream fixture response should serialize");
            response.set_content(*serialized, "application/json");
        }

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
            if (std::holds_alternative<mcp::protocol::JsonRpcNotification>(*parsed)) {
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
                set_response(response,
                             mcp::protocol::JsonRpcResponse{
                                     .id = rpc_request->id,
                                     .result = Json{
                                             {"protocolVersion", std::string(mcp::protocol::McpProtocolVersion)},
                                             {"capabilities", Json{{"tools", Json::object()},
                                                                   {"prompts", Json::object()},
                                                                   {"resources", Json::object()}}},
                                             {"serverInfo", Json{{"name", "http-upstream"}, {"version", "1"}}},
                                     },
                             });
                return;
            }
            if (rpc_request->method == "tools/list") {
                set_response(response,
                             mcp::protocol::JsonRpcResponse{
                                     .id = rpc_request->id,
                                     .result = Json{{"tools", Json::array({
                                                                      Json{{"name", "echo"},
                                                                           {"description", "HTTP echo"},
                                                                           {"inputSchema", Json{{"type", "object"}}}},
                                                              })}},
                             });
                return;
            }
            if (rpc_request->method == "prompts/list") {
                set_response(response,
                             mcp::protocol::JsonRpcResponse{
                                     .id = rpc_request->id,
                                     .result = Json{{"prompts", Json::array({
                                                                        Json{{"name", "summarize"},
                                                                             {"description", "HTTP summarize"},
                                                                             {"arguments", Json::array({
                                                                                                   Json{{"name", "text"},
                                                                                                        {"required", true}},
                                                                                           })}},
                                                                })}},
                             });
                return;
            }
            if (rpc_request->method == "resources/list") {
                set_response(response,
                             mcp::protocol::JsonRpcResponse{
                                     .id = rpc_request->id,
                                     .result = Json{{"resources", Json::array({
                                                                          Json{{"uri", "file:///remote/README.md"},
                                                                               {"name", "RemoteReadme"},
                                                                               {"description", "Remote readme"},
                                                                               {"mimeType", "text/markdown"}},
                                                                  })}},
                             });
                return;
            }
            if (rpc_request->method == "tools/call") {
                set_response(response,
                             mcp::protocol::JsonRpcResponse{
                                     .id = rpc_request->id,
                                     .result = Json{
                                             {"content", Json::array({
                                                                 Json{{"type", "text"}, {"text", "http upstream"}},
                                                         })},
                                             {"structuredContent", rpc_request->params.value("arguments", Json::object())},
                                             {"isError", false},
                                     },
                             });
                return;
            }

            set_response(response,
                         mcp::protocol::JsonRpcResponse{
                                 .id = rpc_request->id,
                                 .error = mcp::protocol::ErrorObject{
                                         .code = static_cast<int>(mcp::protocol::ErrorCode::MethodNotFound),
                                         .message = "method not found",
                                 },
                         });
        }

        httplib::Server server_;
        std::thread thread_;
        int port_ = 0;
        std::atomic<bool> authorization_header_seen_{false};
    };

    void test_lists_tools() {
        CliHarness harness({tool("tool.echo", "echo", true), tool("tool.sleep", "sleep", false)});

        const auto exit_code = harness.run({"tools", "list"});

        require(exit_code == 0, "tools list exit code mismatch");
        require(harness.out.str() == "tool.echo\techo\tenabled\tapproved\tdefault\n"
                                     "tool.sleep\tsleep\tdisabled\tapproved\tdefault\n",
                "tools list output mismatch");
        require(harness.err.str().empty(), "tools list should not write stderr");
    }

    void test_enables_and_disables_tool_policy() {
        CliHarness harness({tool("tool.echo", "echo", false)});

        auto exit_code = harness.run({"tools", "enable", "tool.echo"});
        require(exit_code == 0, "enable exit code mismatch");
        require(harness.tools_.list().front().policy.enabled, "tool should be enabled");

        exit_code = harness.run({"tools", "disable", "tool.echo"});
        require(exit_code == 0, "disable exit code mismatch");
        require(!harness.tools_.list().front().policy.enabled, "tool should be disabled");
    }

    void test_lists_profiles() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile("default", "Default"), profile("dev", "Dev")});

        const auto exit_code = harness.run({"profiles", "list"});

        require(exit_code == 0, "profiles list exit code mismatch");
        require(harness.out.str() == "default\tDefault\t1 endpoint(s)\t1 enabled tool(s)\n"
                                     "dev\tDev\t1 endpoint(s)\t1 enabled tool(s)\n",
                "profiles list output mismatch");
    }

    void test_exports_bundle_through_service() {
        const auto path = std::filesystem::temp_directory_path() / "mcp-cli-export-test.json";
        std::error_code ec;
        std::filesystem::remove(path, ec);

        CliHarness harness({tool("tool.echo", "echo", true), tool("tool.dev", "dev", true, "dev")});

        const auto exit_code = harness.run({"bundle", "export", path.string()});

        require(exit_code == 0, "bundle export exit code mismatch");
        require(harness.out.str() == "Exported bundle default with 1 tool(s)\n", "bundle export output mismatch");

        mcp::app::JsonImportExportService service;
        const auto imported = service.import_bundle(path);
        require(imported.has_value(), "exported bundle should be readable");
        require(imported->profile.id == "default", "exported profile id mismatch");
        require(imported->tools.size() == 1, "exported tool count mismatch");
        require(imported->tools.front().id == "tool.echo", "exported tool id mismatch");

        std::filesystem::remove(path, ec);
    }

    void test_imports_bundle_through_service() {
        const auto path = std::filesystem::temp_directory_path() / "mcp-cli-import-test.json";
        std::error_code ec;
        std::filesystem::remove(path, ec);

        mcp::app::JsonImportExportService service;
        const auto exported = service.export_bundle(
                mcp::app::ExportBundle{
                        .profile = profile("imported", "Imported"),
                        .tools = {tool("tool.imported", "imported", true, "imported")},
                },
                path);
        require(exported.has_value(), "test bundle export failed");

        CliHarness harness({}, {});
        const auto exit_code = harness.run({"bundle", "import", path.string()});

        require(exit_code == 0, "bundle import exit code mismatch");
        require(harness.out.str() == "Imported bundle imported with 1 tool(s)\n", "bundle import output mismatch");
        require(harness.profiles_.list_profiles().size() == 1, "imported profile count mismatch");
        require(harness.tools_.list().size() == 1, "imported tool count mismatch");
        require(harness.tools_.list().front().id == "tool.imported", "imported tool id mismatch");

        std::filesystem::remove(path, ec);
    }

    void test_doctor_reports_empty_runtime() {
        CliHarness harness;

        const auto exit_code = harness.run({"doctor"});

        require(exit_code == 1, "doctor empty runtime exit code mismatch");
        require(harness.out.str().find("runtime\tnot-ready\n") != std::string::npos,
                "doctor empty runtime status mismatch");
        require(harness.out.str().find("servers\t0 configured\n") != std::string::npos,
                "doctor empty runtime server count mismatch");
        require(harness.out.str().find("- No MCP servers configured\n") != std::string::npos,
                "doctor empty runtime server issue mismatch");
        require(harness.out.str().find("hint: add one with: cxxmcp servers import <path>") != std::string::npos,
                "doctor empty runtime server hint mismatch");
        require(harness.out.str().find("- No exposure profiles configured\n") != std::string::npos,
                "doctor empty runtime exposure issue mismatch");
    }

    void test_doctor_reports_ready_runtime_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"}) == 0,
                "exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"doctor"});

        require(exit_code == 0, "json doctor ready runtime exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["ready"], "json doctor ready mismatch");
        require(json["serverCount"] == 1, "json doctor server count mismatch");
        require(json["capabilityCount"] == 1, "json doctor capability count mismatch");
        require(json["exposureProfileCount"] == 1, "json doctor exposure profile count mismatch");
        require(json["profiles"].size() == 1, "json doctor profile count mismatch");
        require(json["profiles"][0]["profileId"] == "profile.dev", "json doctor profile id mismatch");
        require(json["profiles"][0]["ready"], "json doctor profile ready mismatch");
        require(json["profiles"][0]["httpReady"], "json doctor profile http ready mismatch");
        require(json["profiles"][0]["endpointConfigured"], "json doctor endpoint configured mismatch");
        require(json["issues"].empty(), "json doctor top-level issues should be empty");
    }

    void test_doctor_reports_endpointless_gateway_profile_as_not_ready() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "endpointless exposure create failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "endpointless exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"doctor"});

        require(exit_code == 1, "json doctor endpointless profile exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(!json["ready"], "json doctor endpointless ready mismatch");
        require(json["profiles"][0]["ready"], "json doctor endpointless binding readiness mismatch");
        require(!json["profiles"][0]["httpReady"], "json doctor endpointless http ready mismatch");
        require(!json["profiles"][0]["endpointConfigured"],
                "json doctor endpointless endpoint configured mismatch");
        require(json["profiles"][0]["issues"][0]["code"] == "endpoint_not_configured",
                "json doctor endpointless issue mismatch");
    }

    void test_doctor_reports_unready_upstream_health_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        auto trusted = mcp_server("filesystem", "filesystem");
        auto untrusted = mcp_server("remote", "remote");
        untrusted.trust = mcp::app::McpServerTrustState::untrusted;
        require(harness.servers_.save(trusted).has_value(), "trusted server save failed");
        require(harness.servers_.save(untrusted).has_value(), "untrusted server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"}) == 0,
                "exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        harness.err.str({});
        harness.err.clear();
        const auto exit_code = harness.run({"doctor"});

        require(exit_code == 1, "json doctor unready upstream exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(!json["ready"], "json doctor unready upstream ready mismatch");
        require(json["upstreams"]["serverCount"] == 2, "json doctor upstream count mismatch");
        require(json["upstreams"]["readyCount"] == 1, "json doctor upstream ready count mismatch");
        require(!json["upstreams"]["ready"], "json doctor upstream readiness mismatch");
        require(json["upstreams"]["servers"][0]["serverId"] == "filesystem",
                "json doctor upstream first server mismatch");
        require(json["upstreams"]["servers"][0]["ready"], "json doctor upstream first readiness mismatch");
        require(json["upstreams"]["servers"][1]["serverId"] == "remote",
                "json doctor upstream second server mismatch");
        require(!json["upstreams"]["servers"][1]["ready"], "json doctor upstream second readiness mismatch");
        require(json["upstreams"]["servers"][1]["error"] == "cxxmcp server is untrusted",
                "json doctor upstream error mismatch");
        require(json["profiles"][0]["httpReady"], "json doctor gateway profile should remain ready");
    }

    void test_lists_mcp_servers() {
        CliHarness harness;
        const auto saved = harness.servers_.save(mcp_server());
        require(saved.has_value(), "test cxxmcp server save failed");

        const auto exit_code = harness.run({"servers", "list"});

        require(exit_code == 0, "servers list exit code mismatch");
        require(harness.out.str() == "filesystem\tfilesystem\tstdio\tenabled\ttrusted\n",
                "servers list output mismatch");
        require(harness.err.str().empty(), "servers list should not write stderr");
    }

    void test_lists_mcp_servers_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        const auto saved = harness.servers_.save(mcp_server());
        require(saved.has_value(), "test cxxmcp server save failed");

        const auto exit_code = harness.run({"servers", "list"});

        require(exit_code == 0, "json servers list exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json.is_array(), "json servers list should be an array");
        require(json.size() == 1, "json servers list size mismatch");
        require(json[0]["id"] == "filesystem", "json servers list id mismatch");
        require(json[0]["trust"] == "trusted", "json servers list trust mismatch");
    }

    void test_inspects_mcp_server() {
        CliHarness harness;
        auto server = mcp_server();
        server.description = "Filesystem server";
        const auto saved = harness.servers_.save(server);
        require(saved.has_value(), "test cxxmcp server save failed");

        const auto exit_code = harness.run({"servers", "inspect", "filesystem"});

        require(exit_code == 0, "servers inspect exit code mismatch");
        require(harness.out.str().find("id: filesystem\n") != std::string::npos, "servers inspect id mismatch");
        require(harness.out.str().find("display_name: Filesystem\n") != std::string::npos,
                "servers inspect display name mismatch");
        require(harness.out.str().find("trust: trusted\n") != std::string::npos, "servers inspect trust mismatch");
        require(harness.out.str().find("stdio.command: node\n") != std::string::npos,
                "servers inspect command mismatch");
    }

    void test_inspects_mcp_server_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        const auto saved = harness.servers_.save(mcp_server());
        require(saved.has_value(), "test cxxmcp server save failed");

        const auto exit_code = harness.run({"servers", "inspect", "filesystem"});

        require(exit_code == 0, "json servers inspect exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["id"] == "filesystem", "json servers inspect id mismatch");
        require(json["stdio"]["command"] == "node", "json servers inspect command mismatch");
    }

    void test_inspects_mcp_server_usage_context() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"}) == 0,
                "exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"servers", "inspect", "filesystem"});

        require(exit_code == 0, "servers inspect usage context exit code mismatch");
        require(harness.out.str().find("capabilities: 1 discovered\n") != std::string::npos,
                "servers inspect capability count mismatch");
        require(harness.out.str().find("exposure_bindings: 1\n") != std::string::npos,
                "servers inspect exposure binding count mismatch");
        require(harness.out.str().find("exposure: profile.dev\tready\t1 binding(s)\t1 enabled binding(s)\n") !=
                        std::string::npos,
                "servers inspect exposure context mismatch");
    }

    void test_inspects_mcp_server_usage_context_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"}) == 0,
                "untrusted exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"servers", "inspect", "filesystem"});

        require(exit_code == 0, "json servers inspect usage context exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["capabilityCount"] == 1, "json servers inspect capability count mismatch");
        require(json["exposureBindingCount"] == 1, "json servers inspect exposure binding count mismatch");
        require(json["exposureProfiles"].size() == 1, "json servers inspect exposure profile count mismatch");
        require(json["exposureProfiles"][0]["profileId"] == "profile.dev",
                "json servers inspect exposure profile id mismatch");
        require(json["exposureProfiles"][0]["ready"], "json servers inspect exposure readiness mismatch");
    }

    void test_imports_mcp_servers_from_client_config() {
        const auto path = std::filesystem::temp_directory_path() / "mcp-cli-client-config-test.json";
        std::error_code ec;
        std::filesystem::remove(path, ec);

        {
            std::ofstream output(path);
            output << R"({
  "mcpServers": {
    "filesystem": {
      "command": "node",
      "args": ["server.js"],
      "cwd": "C:/workspace",
      "env": {"ROOT": "C:/workspace"}
    }
  }
})";
        }

        CliHarness harness;
        const auto exit_code = harness.run({"servers", "import", path.string()});

        require(exit_code == 0, "servers import exit code mismatch");
        require(harness.out.str() == "Imported 1 cxxmcp server(s)\n", "servers import output mismatch");
        const auto servers = harness.servers_.list_servers();
        require(servers.size() == 1, "Imported MCP server count mismatch");
        require(servers.front().id == "filesystem", "Imported MCP server id mismatch");
        require(servers.front().stdio.command == "node", "Imported MCP server command mismatch");
        require(servers.front().stdio.args.size() == 1 && servers.front().stdio.args.front() == "server.js",
                "Imported MCP server args mismatch");

        std::filesystem::remove(path, ec);
    }

    void test_imports_mcp_servers_from_client_config_as_json() {
        const auto path = std::filesystem::temp_directory_path() / "mcp-cli-client-config-json-test.json";
        std::error_code ec;
        std::filesystem::remove(path, ec);

        {
            std::ofstream output(path);
            output << R"({
  "mcpServers": {
    "filesystem": {
      "command": "node",
      "args": ["server.js"]
    }
  }
})";
        }

        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        const auto exit_code = harness.run({"servers", "import", path.string()});

        require(exit_code == 0, "json servers import exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["importedCount"] == 1, "json servers import count mismatch");
        require(json["servers"].size() == 1, "json servers import server count mismatch");
        require(json["servers"][0]["id"] == "filesystem", "json servers import id mismatch");
        require(json["servers"][0]["stdio"]["command"] == "node", "json servers import command mismatch");

        std::filesystem::remove(path, ec);
    }

    void test_imports_mcp_servers_with_trust_and_discovery_as_json() {
        const auto path = std::filesystem::temp_directory_path() / "mcp-cli-client-config-discover-test.json";
        std::error_code ec;
        std::filesystem::remove(path, ec);

        {
            std::ofstream output(path);
            output << R"({
  "mcpServers": {
    "filesystem": {
      "command": "node",
      "args": ["server.js"]
    }
  }
})";
        }

        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        const auto exit_code = harness.run({"servers", "import", "--trust", "--discover", path.string()});

        require(exit_code == 0, "json servers import trust discover exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["importedCount"] == 1, "json servers import trust discover count mismatch");
        require(json["trusted"], "json servers import trust flag mismatch");
        require(json["discovered"], "json servers import discovery flag mismatch");
        require(json["discoveredCapabilityCount"] == 3,
                "json servers import discovered capability count mismatch");
        require(json["discoveryReports"].size() == 1,
                "json servers import discovery report count mismatch");
        require(json["discoveryReports"][0]["serverId"] == "filesystem",
                "json servers import discovery report server mismatch");
        require(json["servers"][0]["trust"] == "trusted", "json servers import trust state mismatch");
        require(harness.capabilities_.list_capabilities().size() == 3,
                "json servers import should store discovered capabilities");

        std::filesystem::remove(path, ec);
    }

    void test_adds_stdio_mcp_server() {
        CliHarness harness;

        const auto exit_code = harness.run({"servers", "add-stdio", "filesystem", "node", "server.js", "--root", "C:/workspace"});

        require(exit_code == 0, "servers add-stdio exit code mismatch");
        require(harness.out.str() == "Added stdio MCP server filesystem\n", "servers add-stdio output mismatch");

        const auto servers = harness.servers_.list_servers();
        require(servers.size() == 1, "added stdio server count mismatch");
        require(servers.front().id == "filesystem", "added stdio server id mismatch");
        require(servers.front().transport == mcp::app::McpServerTransportKind::stdio,
                "added stdio server transport mismatch");
        require(servers.front().stdio.command == "node", "added stdio server command mismatch");
        require(servers.front().stdio.args.size() == 3, "added stdio server args mismatch");
        require(servers.front().stdio.args[0] == "server.js", "added stdio server first arg mismatch");
        require(servers.front().trust == mcp::app::McpServerTrustState::untrusted,
                "added stdio server should default to untrusted");
    }

    void test_adds_stdio_mcp_server_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);

        const auto exit_code =
                harness.run({"servers", "add-stdio", "filesystem", "node", "server.js", "--root", "C:/workspace"});

        require(exit_code == 0, "json servers add-stdio exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["id"] == "filesystem", "json servers add-stdio id mismatch");
        require(json["transport"] == "stdio", "json servers add-stdio transport mismatch");
        require(json["stdio"]["command"] == "node", "json servers add-stdio command mismatch");
        require(json["stdio"]["args"].size() == 3, "json servers add-stdio args mismatch");
        require(json["trust"] == "untrusted", "json servers add-stdio trust mismatch");
    }

    void test_adds_stdio_mcp_server_with_launch_options_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);

        const auto exit_code =
                harness.run({"servers",
                             "add-stdio",
                             "--trust",
                             "--discover",
                             "--cwd",
                             "C:/workspace",
                             "--env",
                             "API_TOKEN",
                             "secret",
                             "--env",
                             "MCP_PROFILE",
                             "dev",
                             "filesystem",
                             "node",
                             "server.js",
                             "--root",
                             "C:/workspace"});

        require(exit_code == 0, "json servers add-stdio with launch options exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["id"] == "filesystem", "json servers add-stdio with launch options id mismatch");
        require(json["stdio"]["command"] == "node",
                "json servers add-stdio with launch options command mismatch");
        require(json["stdio"]["cwd"] == "C:/workspace",
                "json servers add-stdio with launch options cwd mismatch");
        require(json["stdio"]["env"]["API_TOKEN"] == "secret",
                "json servers add-stdio with launch options token env mismatch");
        require(json["stdio"]["env"]["MCP_PROFILE"] == "dev",
                "json servers add-stdio with launch options profile env mismatch");
        require(json["stdio"]["args"].size() == 3,
                "json servers add-stdio with launch options args mismatch");
        require(json["trust"] == "trusted", "json servers add-stdio with launch options trust mismatch");
        require(json["discovered"], "json servers add-stdio with launch options discovery flag mismatch");
        require(json["discoveredCapabilityCount"] == 3,
                "json servers add-stdio with launch options discovery count mismatch");
        require(harness.capabilities_.list_capabilities().size() == 3,
                "json servers add-stdio with launch options should save capabilities");
    }

    void test_adds_http_mcp_server() {
        CliHarness harness;

        const auto exit_code = harness.run({"servers", "add-http", "remote", "http://127.0.0.1:3000/mcp"});

        require(exit_code == 0, "servers add-http exit code mismatch");
        require(harness.out.str() == "Added HTTP MCP server remote\n", "servers add-http output mismatch");

        const auto servers = harness.servers_.list_servers();
        require(servers.size() == 1, "added http server count mismatch");
        require(servers.front().id == "remote", "added http server id mismatch");
        require(servers.front().transport == mcp::app::McpServerTransportKind::streamable_http,
                "added http server transport mismatch");
        require(servers.front().http.url == "http://127.0.0.1:3000/mcp", "added http server url mismatch");
        require(servers.front().trust == mcp::app::McpServerTrustState::untrusted,
                "added http server should default to untrusted");
    }

    void test_adds_http_mcp_server_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);

        const auto exit_code = harness.run({"servers", "add-http", "remote", "http://127.0.0.1:3000/mcp"});

        require(exit_code == 0, "json servers add-http exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["id"] == "remote", "json servers add-http id mismatch");
        require(json["transport"] == "streamable_http", "json servers add-http transport mismatch");
        require(json["http"]["url"] == "http://127.0.0.1:3000/mcp", "json servers add-http url mismatch");
        require(json["trust"] == "untrusted", "json servers add-http trust mismatch");
    }

    void test_adds_http_mcp_server_with_headers_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);

        const auto exit_code =
                harness.run({"servers",
                             "add-http",
                             "--trust",
                             "--discover",
                             "--header",
                             "Authorization",
                             "Bearer token",
                             "--header",
                             "X-Tenant",
                             "dev",
                             "remote",
                             "http://127.0.0.1:3000/mcp"});

        require(exit_code == 0, "json servers add-http with headers exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["id"] == "remote", "json servers add-http with headers id mismatch");
        require(json["http"]["url"] == "http://127.0.0.1:3000/mcp",
                "json servers add-http with headers url mismatch");
        require(json["http"]["headers"]["Authorization"] == "Bearer token",
                "json servers add-http authorization header mismatch");
        require(json["http"]["headers"]["X-Tenant"] == "dev",
                "json servers add-http tenant header mismatch");
        require(json["trust"] == "trusted", "json servers add-http with headers trust mismatch");
        require(json["discovered"], "json servers add-http with headers discovery flag mismatch");
        require(json["discoveredCapabilityCount"] == 3,
                "json servers add-http with headers discovery count mismatch");
        require(harness.capabilities_.list_capabilities().size() == 3,
                "json servers add-http with headers should save capabilities");
    }

    void test_configures_stdio_mcp_server_launch_environment() {
        CliHarness harness;
        require(harness.run({"servers", "add-stdio", "filesystem", "node", "server.js"}) == 0,
                "servers add-stdio failed");

        harness.out.str({});
        harness.out.clear();
        auto exit_code = harness.run({"servers", "set-cwd", "filesystem", "C:/workspace"});
        require(exit_code == 0, "servers set-cwd exit code mismatch");
        require(harness.out.str() == "Set cxxmcp server filesystem cwd to C:/workspace\n",
                "servers set-cwd output mismatch");
        require(harness.servers_.list_servers().front().stdio.cwd == "C:/workspace",
                "servers set-cwd stored value mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"servers", "set-env", "filesystem", "API_TOKEN", "secret"});
        require(exit_code == 0, "servers set-env exit code mismatch");
        require(harness.out.str() == "Set cxxmcp server filesystem env API_TOKEN\n",
                "servers set-env output mismatch");
        require(harness.servers_.list_servers().front().stdio.env.at("API_TOKEN") == "secret",
                "servers set-env stored value mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"servers", "inspect", "filesystem"});
        require(exit_code == 0, "servers inspect configured stdio exit code mismatch");
        require(harness.out.str().find("stdio.cwd: C:/workspace\n") != std::string::npos,
                "servers inspect cwd mismatch");
        require(harness.out.str().find("  API_TOKEN=secret\n") != std::string::npos,
                "servers inspect env mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"servers", "unset-env", "filesystem", "API_TOKEN"});
        require(exit_code == 0, "servers unset-env exit code mismatch");
        require(harness.out.str() == "Unset cxxmcp server filesystem env API_TOKEN\n",
                "servers unset-env output mismatch");
        require(!harness.servers_.list_servers().front().stdio.env.contains("API_TOKEN"),
                "servers unset-env should remove env value");
    }

    void test_configures_stdio_mcp_server_launch_environment_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");

        auto exit_code = harness.run({"servers", "set-cwd", "filesystem", "C:/workspace"});
        require(exit_code == 0, "json servers set-cwd exit code mismatch");
        auto json = Json::parse(harness.out.str());
        require(json["id"] == "filesystem", "json servers set-cwd id mismatch");
        require(json["stdio"]["cwd"] == "C:/workspace", "json servers set-cwd value mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"servers", "set-env", "filesystem", "API_TOKEN", "secret"});
        require(exit_code == 0, "json servers set-env exit code mismatch");
        json = Json::parse(harness.out.str());
        require(json["stdio"]["env"]["API_TOKEN"] == "secret", "json servers set-env value mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"servers", "unset-env", "filesystem", "API_TOKEN"});
        require(exit_code == 0, "json servers unset-env exit code mismatch");
        json = Json::parse(harness.out.str());
        require(!json["stdio"]["env"].contains("API_TOKEN"), "json servers unset-env should remove value");
    }

    void test_configures_http_mcp_server_headers() {
        CliHarness harness;
        require(harness.run({"servers", "add-http", "remote", "http://127.0.0.1:3000/mcp"}) == 0,
                "servers add-http failed");

        harness.out.str({});
        harness.out.clear();
        auto exit_code = harness.run({"servers", "set-header", "remote", "Authorization", "Bearer token"});
        require(exit_code == 0, "servers set-header exit code mismatch");
        require(harness.out.str() == "Set cxxmcp server remote header Authorization\n",
                "servers set-header output mismatch");
        require(harness.servers_.list_servers().front().http.headers.at("Authorization") == "Bearer token",
                "servers set-header stored value mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"servers", "inspect", "remote"});
        require(exit_code == 0, "servers inspect configured http exit code mismatch");
        require(harness.out.str().find("http.headers:\n") != std::string::npos,
                "servers inspect headers section mismatch");
        require(harness.out.str().find("  Authorization=Bearer token\n") != std::string::npos,
                "servers inspect header mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"servers", "unset-header", "remote", "Authorization"});
        require(exit_code == 0, "servers unset-header exit code mismatch");
        require(harness.out.str() == "Unset cxxmcp server remote header Authorization\n",
                "servers unset-header output mismatch");
        require(!harness.servers_.list_servers().front().http.headers.contains("Authorization"),
                "servers unset-header should remove header value");
    }

    void test_configures_http_mcp_server_headers_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        require(harness.run({"servers", "add-http", "remote", "http://127.0.0.1:3000/mcp"}) == 0,
                "json servers add-http failed");

        harness.out.str({});
        harness.out.clear();
        auto exit_code = harness.run({"servers", "set-header", "remote", "Authorization", "Bearer token"});
        require(exit_code == 0, "json servers set-header exit code mismatch");
        auto json = Json::parse(harness.out.str());
        require(json["http"]["headers"]["Authorization"] == "Bearer token",
                "json servers set-header value mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"servers", "unset-header", "remote", "Authorization"});
        require(exit_code == 0, "json servers unset-header exit code mismatch");
        json = Json::parse(harness.out.str());
        require(!json["http"]["headers"].contains("Authorization"),
                "json servers unset-header should remove value");
    }

    void test_updates_mcp_server_control_state() {
        CliHarness harness;
        const auto saved = harness.servers_.save(mcp_server());
        require(saved.has_value(), "test cxxmcp server save failed");

        auto exit_code = harness.run({"servers", "untrust", "filesystem"});
        require(exit_code == 0, "servers untrust exit code mismatch");
        require(harness.out.str() == "Set cxxmcp server filesystem trust to untrusted\n",
                "servers untrust output mismatch");
        require(harness.servers_.list_servers().front().trust == mcp::app::McpServerTrustState::untrusted,
                "server should be untrusted");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"servers", "block", "filesystem"});
        require(exit_code == 0, "servers block exit code mismatch");
        require(harness.out.str() == "Set cxxmcp server filesystem trust to blocked\n", "servers block output mismatch");
        require(harness.servers_.list_servers().front().trust == mcp::app::McpServerTrustState::blocked,
                "server should be blocked");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"servers", "trust", "filesystem"});
        require(exit_code == 0, "servers trust exit code mismatch");
        require(harness.out.str() == "Set cxxmcp server filesystem trust to trusted\n", "servers trust output mismatch");
        require(harness.servers_.list_servers().front().trust == mcp::app::McpServerTrustState::trusted,
                "server should be trusted");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"servers", "disable", "filesystem"});
        require(exit_code == 0, "servers disable exit code mismatch");
        require(harness.out.str() == "Disabled cxxmcp server filesystem\n", "servers disable output mismatch");
        require(!harness.servers_.list_servers().front().enabled, "server should be disabled");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"servers", "enable", "filesystem"});
        require(exit_code == 0, "servers enable exit code mismatch");
        require(harness.out.str() == "Enabled cxxmcp server filesystem\n", "servers enable output mismatch");
        require(harness.servers_.list_servers().front().enabled, "server should be enabled");

        const auto capabilities_saved = harness.capabilities_.replace_for_server(
                "filesystem",
                {
                        capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                   "filesystem.read_file"),
                });
        require(capabilities_saved.has_value(), "test cxxmcp server capability save failed");

        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        harness.out.str({});
        harness.out.clear();
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"servers", "remove", "filesystem"});
        require(exit_code == 0, "servers remove exit code mismatch");
        require(harness.out.str() == "Removed cxxmcp server filesystem\n", "servers remove output mismatch");
        require(harness.servers_.list_servers().empty(), "server should be removed");
        require(harness.capabilities_.list_capabilities().empty(), "removed server capabilities should be cleared");
        require(harness.exposure_profiles_.list_exposure_profiles().front().bindings.empty(),
                "removed server bindings should be cleared from exposure profile");
    }

    void test_updates_mcp_server_control_state_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");

        auto exit_code = harness.run({"servers", "untrust", "filesystem"});
        require(exit_code == 0, "json servers untrust exit code mismatch");
        auto json = Json::parse(harness.out.str());
        require(json["id"] == "filesystem", "json servers untrust id mismatch");
        require(json["trust"] == "untrusted", "json servers untrust value mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"servers", "trust", "filesystem"});
        require(exit_code == 0, "json servers trust exit code mismatch");
        json = Json::parse(harness.out.str());
        require(json["trust"] == "trusted", "json servers trust value mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"servers", "disable", "filesystem"});
        require(exit_code == 0, "json servers disable exit code mismatch");
        json = Json::parse(harness.out.str());
        require(!json["enabled"], "json servers disable value mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"servers", "enable", "filesystem"});
        require(exit_code == 0, "json servers enable exit code mismatch");
        json = Json::parse(harness.out.str());
        require(json["enabled"], "json servers enable value mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"servers", "remove", "filesystem"});
        require(exit_code == 0, "json servers remove exit code mismatch");
        json = Json::parse(harness.out.str());
        require(json["serverId"] == "filesystem", "json servers remove id mismatch");
        require(json["removed"], "json servers remove flag mismatch");
        require(harness.servers_.list_servers().empty(), "json servers remove should remove server");
    }

    void test_discovers_mcp_server_capabilities() {
        CliHarness harness;
        const auto saved = harness.servers_.save(mcp_server());
        require(saved.has_value(), "test cxxmcp server save failed");

        const auto exit_code = harness.run({"servers", "discover", "filesystem"});

        require(exit_code == 0, "servers discover exit code mismatch");
        require(harness.out.str() == "Discovered 3 capability(s) from filesystem\n",
                "servers discover output mismatch");
        const auto capabilities = harness.capabilities_.list_capabilities();
        require(capabilities.size() == 3, "discovered capability count mismatch");
        require(capabilities.front().id == "filesystem:tool:read_file", "discovered tool id mismatch");
    }

    void test_discovers_mcp_server_capabilities_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");

        const auto exit_code = harness.run({"servers", "discover", "filesystem"});

        require(exit_code == 0, "json servers discover exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["serverId"] == "filesystem", "json servers discover id mismatch");
        require(json["discovered"], "json servers discover flag mismatch");
        require(json["capabilityCount"] == 3, "json servers discover count mismatch");
        require(harness.capabilities_.list_capabilities().size() == 3,
                "json servers discover should save capabilities");
    }

    void test_discover_mcp_server_reports_trust_hint() {
        CliHarness harness;
        auto server = mcp_server();
        server.trust = mcp::app::McpServerTrustState::untrusted;
        require(harness.servers_.save(server).has_value(), "test untrusted cxxmcp server save failed");

        const auto exit_code = harness.run({"servers", "discover", "filesystem"});

        require(exit_code == 1, "servers discover untrusted exit code mismatch");
        require(harness.err.str().find("cxxmcp server is untrusted: filesystem") != std::string::npos,
                "servers discover untrusted error mismatch");
        require(harness.err.str().find("hint: trust the server with: cxxmcp servers trust filesystem") !=
                        std::string::npos,
                "servers discover untrusted hint mismatch");
    }

    void test_discovers_all_mcp_servers_with_skips() {
        CliHarness harness;
        auto trusted = mcp_server("filesystem", "filesystem");
        auto untrusted = mcp_server("remote", "remote");
        untrusted.trust = mcp::app::McpServerTrustState::untrusted;
        require(harness.servers_.save(trusted).has_value(), "trusted server save failed");
        require(harness.servers_.save(untrusted).has_value(), "untrusted server save failed");

        const auto exit_code = harness.run({"servers", "discover-all"});

        require(exit_code == 0, "servers discover-all exit code mismatch");
        require(harness.out.str() == "filesystem\tdiscovered\t3 capability(s)\n"
                                     "remote\tskipped\tcxxmcp server is untrusted: remote\n",
                "servers discover-all output mismatch");
        require(harness.capabilities_.list_capabilities().size() == 3,
                "servers discover-all should save trusted capabilities only");
    }

    void test_discovers_all_mcp_servers_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        auto trusted = mcp_server("filesystem", "filesystem");
        auto untrusted = mcp_server("remote", "remote");
        untrusted.trust = mcp::app::McpServerTrustState::untrusted;
        require(harness.servers_.save(trusted).has_value(), "trusted server save failed");
        require(harness.servers_.save(untrusted).has_value(), "untrusted server save failed");

        const auto exit_code = harness.run({"servers", "discover-all"});

        require(exit_code == 0, "json servers discover-all exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json.is_array(), "json servers discover-all should be an array");
        require(json.size() == 2, "json servers discover-all count mismatch");
        require(json[0]["serverId"] == "filesystem", "json servers discover-all first id mismatch");
        require(json[0]["discovered"], "json servers discover-all first status mismatch");
        require(json[0]["capabilityCount"] == 3, "json servers discover-all capability count mismatch");
        require(json[1]["serverId"] == "remote", "json servers discover-all second id mismatch");
        require(!json[1]["discovered"], "json servers discover-all skipped status mismatch");
        require(json[1]["error"] == "cxxmcp server is untrusted", "json servers discover-all error mismatch");
    }

    void test_checks_mcp_server_health() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");

        const auto exit_code = harness.run({"servers", "check", "filesystem"});

        require(exit_code == 0, "servers check exit code mismatch");
        require(harness.out.str() == "filesystem\tready\t3 capability(s)\n", "servers check output mismatch");
        require(harness.err.str().empty(), "servers check should not write stderr");
        require(harness.capabilities_.list_capabilities().empty(), "servers check should not save capabilities");
    }

    void test_checks_mcp_server_health_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");

        const auto exit_code = harness.run({"servers", "check", "filesystem"});

        require(exit_code == 0, "json servers check exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["serverId"] == "filesystem", "json servers check id mismatch");
        require(json["ready"], "json servers check readiness mismatch");
        require(json["capabilityCount"] == 3, "json servers check count mismatch");
        require(harness.capabilities_.list_capabilities().empty(), "json servers check should not save capabilities");
    }

    void test_checks_all_mcp_servers_health() {
        CliHarness harness;
        auto trusted = mcp_server("filesystem", "filesystem");
        auto untrusted = mcp_server("remote", "remote");
        untrusted.trust = mcp::app::McpServerTrustState::untrusted;
        require(harness.servers_.save(trusted).has_value(), "trusted server save failed");
        require(harness.servers_.save(untrusted).has_value(), "untrusted server save failed");

        const auto exit_code = harness.run({"servers", "check-all"});

        require(exit_code == 1, "servers check-all should fail when a server is unready");
        require(harness.out.str() == "filesystem\tready\t3 capability(s)\n"
                                     "remote\tnot-ready\t0 capability(s)\n",
                "servers check-all output mismatch");
        require(harness.err.str().find("cxxmcp server is untrusted: remote") != std::string::npos,
                "servers check-all untrusted error mismatch");
        require(harness.err.str().find("hint: trust the server with: cxxmcp servers trust remote") != std::string::npos,
                "servers check-all untrusted hint mismatch");
    }

    void test_checks_all_mcp_servers_health_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        auto trusted = mcp_server("filesystem", "filesystem");
        auto untrusted = mcp_server("remote", "remote");
        untrusted.trust = mcp::app::McpServerTrustState::untrusted;
        require(harness.servers_.save(trusted).has_value(), "trusted server save failed");
        require(harness.servers_.save(untrusted).has_value(), "untrusted server save failed");

        const auto exit_code = harness.run({"servers", "check-all"});

        require(exit_code == 1, "json servers check-all should fail when a server is unready");
        const auto json = Json::parse(harness.out.str());
        require(json.is_array(), "json servers check-all should be an array");
        require(json.size() == 2, "json servers check-all count mismatch");
        require(json[0]["serverId"] == "filesystem", "json servers check-all first id mismatch");
        require(json[0]["ready"], "json servers check-all first readiness mismatch");
        require(json[0]["capabilityCount"] == 3, "json servers check-all first count mismatch");
        require(json[1]["serverId"] == "remote", "json servers check-all second id mismatch");
        require(!json[1]["ready"], "json servers check-all second readiness mismatch");
        require(json[1]["error"] == "cxxmcp server is untrusted", "json servers check-all error mismatch");
    }

    void test_lists_discovered_capabilities() {
        CliHarness harness;
        const auto saved = harness.capabilities_.replace_for_server(
                "filesystem",
                {
                        capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                   "filesystem.read_file"),
                        capability("filesystem:prompt:summarize", mcp::app::CapabilityKind::prompt, "summarize",
                                   "filesystem.summarize"),
                });
        require(saved.has_value(), "test capability save failed");

        const auto exit_code = harness.run({"capabilities", "list"});

        require(exit_code == 0, "capabilities list exit code mismatch");
        require(harness.out.str() == "filesystem:tool:read_file\ttool\tfilesystem\tread_file\tfilesystem.read_file\n"
                                     "filesystem:prompt:summarize\tprompt\tfilesystem\tsummarize\tfilesystem.summarize\n",
                "capabilities list output mismatch");
        require(harness.err.str().empty(), "capabilities list should not write stderr");
    }

    void test_lists_discovered_capabilities_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        const auto saved = harness.capabilities_.replace_for_server(
                "filesystem",
                {
                        capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                   "filesystem.read_file"),
                });
        require(saved.has_value(), "test capability save failed");

        const auto exit_code = harness.run({"capabilities", "list"});

        require(exit_code == 0, "json capabilities list exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json.is_array(), "json capabilities list should be an array");
        require(json.size() == 1, "json capabilities list size mismatch");
        require(json[0]["id"] == "filesystem:tool:read_file", "json capabilities list id mismatch");
    }

    void test_inspects_discovered_capability() {
        CliHarness harness;
        auto read_file = capability("filesystem:tool:read_file",
                                    mcp::app::CapabilityKind::tool,
                                    "read_file",
                                    "filesystem.read_file");
        read_file.title = "Read File";
        read_file.description = "Read a local file";
        read_file.input_schema = Json{{"type", "object"}, {"required", Json::array({"path"})}};
        const auto saved = harness.capabilities_.replace_for_server("filesystem", {read_file});
        require(saved.has_value(), "test capability save failed");

        const auto exit_code = harness.run({"capabilities", "inspect", "filesystem:tool:read_file"});

        require(exit_code == 0, "capabilities inspect exit code mismatch");
        require(harness.out.str().find("id: filesystem:tool:read_file\n") != std::string::npos,
                "capabilities inspect id mismatch");
        require(harness.out.str().find("kind: tool\n") != std::string::npos,
                "capabilities inspect kind mismatch");
        require(harness.out.str().find("server_id: filesystem\n") != std::string::npos,
                "capabilities inspect server mismatch");
        require(harness.out.str().find("upstream_name: read_file\n") != std::string::npos,
                "capabilities inspect upstream mismatch");
        require(harness.out.str().find("exposed_name: filesystem.read_file\n") != std::string::npos,
                "capabilities inspect exposed mismatch");
        require(harness.out.str().find("description: Read a local file\n") != std::string::npos,
                "capabilities inspect description mismatch");
        require(harness.out.str().find("input_schema:\n") != std::string::npos,
                "capabilities inspect schema header mismatch");
        require(harness.out.str().find("\"required\"") != std::string::npos,
                "capabilities inspect schema body mismatch");
    }

    void test_inspects_discovered_capability_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        auto resource = capability("filesystem:resource:file:///tmp/readme.md",
                                   mcp::app::CapabilityKind::resource,
                                   "file:///tmp/readme.md",
                                   "file:///tmp/readme.md");
        resource.uri = "file:///tmp/readme.md";
        const auto saved = harness.capabilities_.replace_for_server("filesystem", {resource});
        require(saved.has_value(), "test capability save failed");

        const auto exit_code = harness.run({"capabilities", "inspect", "filesystem:resource:file:///tmp/readme.md"});

        require(exit_code == 0, "json capabilities inspect exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["id"] == "filesystem:resource:file:///tmp/readme.md",
                "json capabilities inspect id mismatch");
        require(json["kind"] == "resource", "json capabilities inspect kind mismatch");
        require(json["uri"] == "file:///tmp/readme.md", "json capabilities inspect uri mismatch");
    }

    void test_creates_and_lists_exposure_profiles() {
        CliHarness harness;

        auto exit_code = harness.run({"exposures", "create", "profile.dev", "Dev"});
        require(exit_code == 0, "exposures create exit code mismatch");
        require(harness.out.str() == "Created exposure profile profile.dev\n", "exposures create output mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"});
        require(exit_code == 0, "exposures endpoint exit code mismatch");
        require(harness.out.str() == "Configured exposure endpoint profile.dev at http://127.0.0.1:39919/cxxmcp/dev\n",
                "exposures endpoint output mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"exposures", "list"});

        require(exit_code == 0, "exposures list exit code mismatch");
        require(harness.out.str() == "profile.dev\tDev\t127.0.0.1:39919\t/cxxmcp/dev\t0 binding(s)\n",
                "exposures list output mismatch");
    }

    void test_gateway_lists_and_inspects_profiles() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"}) == 0,
                "exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        auto exit_code = harness.run({"gateway", "list"});
        require(exit_code == 0, "gateway list exit code mismatch");
        require(harness.out.str() == "profile.dev\tDev\t127.0.0.1:39919\t/cxxmcp/dev\t1 binding(s)\n",
                "gateway list output mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"gateway", "inspect", "profile.dev"});
        require(exit_code == 0, "gateway inspect exit code mismatch");
        require(harness.out.str().find("endpoint: http://127.0.0.1:39919/cxxmcp/dev\n") != std::string::npos,
                "gateway inspect endpoint mismatch");
        require(harness.out.str().find("readiness: ready\n") != std::string::npos,
                "gateway inspect readiness mismatch");
        require(harness.out.str().find("http-readiness: ready\n") != std::string::npos,
                "gateway inspect http readiness mismatch");
        require(harness.out.str().find("binding: profile.dev:filesystem:tool:read_file") != std::string::npos,
                "gateway inspect binding mismatch");
    }

    void test_gateway_lists_and_inspects_profiles_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "json exposure create failed");

        harness.out.str({});
        harness.out.clear();
        auto exit_code = harness.run({"gateway", "list"});
        require(exit_code == 0, "json gateway list exit code mismatch");
        auto json = Json::parse(harness.out.str());
        require(json.is_array() && json.size() == 1, "json gateway list size mismatch");
        require(json[0]["id"] == "profile.dev", "json gateway list id mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"gateway", "inspect", "profile.dev"});
        require(exit_code == 0, "json gateway inspect exit code mismatch");
        json = Json::parse(harness.out.str());
        require(json["id"] == "profile.dev", "json gateway inspect id mismatch");
        require(json.contains("readiness"), "json gateway inspect readiness missing");
        require(json.contains("gatewayStatus"), "json gateway inspect status missing");
        require(!json["gatewayStatus"]["httpReady"], "json gateway inspect http readiness mismatch");
        require(json["gatewayStatus"]["issues"][1]["code"] == "endpoint_not_configured",
                "json gateway inspect endpoint issue mismatch");
    }

    void test_creates_and_configures_exposure_profile_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);

        auto exit_code = harness.run({"exposures", "create", "profile.dev", "Dev"});
        require(exit_code == 0, "json exposures create exit code mismatch");
        auto json = Json::parse(harness.out.str());
        require(json["id"] == "profile.dev", "json exposures create id mismatch");
        require(json["name"] == "Dev", "json exposures create name mismatch");
        require(json["bindings"].empty(), "json exposures create bindings mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"});
        require(exit_code == 0, "json exposures endpoint exit code mismatch");
        json = Json::parse(harness.out.str());
        require(json["endpoint"]["listenHost"] == "127.0.0.1", "json exposures endpoint host mismatch");
        require(json["endpoint"]["listenPort"] == 39919, "json exposures endpoint port mismatch");
        require(json["endpoint"]["path"] == "/cxxmcp/dev", "json exposures endpoint path mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"exposures", "set-instructions", "profile.dev", "Use reviewed tools only."});
        require(exit_code == 0, "json exposures set-instructions exit code mismatch");
        json = Json::parse(harness.out.str());
        require(json["instructions"] == "Use reviewed tools only.",
                "json exposures set-instructions value mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"exposures", "clear-instructions", "profile.dev"});
        require(exit_code == 0, "json exposures clear-instructions exit code mismatch");
        json = Json::parse(harness.out.str());
        require(json["instructions"] == "", "json exposures clear-instructions value mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"exposures", "remove", "profile.dev"});
        require(exit_code == 0, "json exposures remove exit code mismatch");
        json = Json::parse(harness.out.str());
        require(json["profileId"] == "profile.dev", "json exposures remove id mismatch");
        require(json["removed"], "json exposures remove flag mismatch");
        require(harness.exposure_profiles_.list_exposure_profiles().empty(),
                "json exposures remove should delete profile");
    }

    void test_binds_capability_to_exposure_profile() {
        CliHarness harness;
        const auto saved = harness.capabilities_.replace_for_server(
                "filesystem",
                {
                        capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                   "filesystem.read_file"),
                });
        require(saved.has_value(), "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code =
                harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file", "dev.read_file"});

        require(exit_code == 0, "exposures bind exit code mismatch");
        require(harness.out.str() ==
                        "Bound capability filesystem:tool:read_file to profile.dev as dev.read_file\n",
                "exposures bind output mismatch");

        const auto profiles = harness.exposure_profiles_.list_exposure_profiles();
        require(profiles.size() == 1, "bound exposure profile count mismatch");
        require(profiles.front().bindings.size() == 1, "bound exposure profile binding count mismatch");
        require(profiles.front().bindings.front().server_id == "filesystem", "bound capability server mismatch");
        require(profiles.front().bindings.front().upstream_name == "read_file", "bound capability upstream mismatch");
        require(profiles.front().bindings.front().exposed_name == "dev.read_file", "bound capability exposed mismatch");
    }

    void test_binds_capability_to_exposure_profile_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "json exposure create failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code =
                harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file", "dev.read_file"});

        require(exit_code == 0, "json exposures bind exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["id"] == "profile.dev", "json exposures bind profile mismatch");
        require(json["bindings"].size() == 1, "json exposures bind count mismatch");
        require(json["bindings"][0]["serverId"] == "filesystem", "json exposures bind server mismatch");
        require(json["bindings"][0]["upstreamName"] == "read_file", "json exposures bind upstream mismatch");
        require(json["bindings"][0]["exposedName"] == "dev.read_file", "json exposures bind exposed mismatch");
    }

    void test_sets_exposure_profile_instructions() {
        CliHarness harness;
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");

        harness.out.str({});
        harness.out.clear();
        auto exit_code = harness.run({"exposures", "set-instructions", "profile.dev",
                                      "Use this gateway for reviewed workspace tools only."});

        require(exit_code == 0, "exposures set-instructions exit code mismatch");
        require(harness.out.str() == "Set exposure profile profile.dev instructions\n",
                "exposures set-instructions output mismatch");
        require(harness.exposure_profiles_.list_exposure_profiles().front().instructions ==
                        "Use this gateway for reviewed workspace tools only.",
                "exposures set-instructions stored value mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"exposures", "inspect", "profile.dev"});
        require(exit_code == 0, "exposures inspect instructions exit code mismatch");
        require(harness.out.str().find(
                        "instructions: Use this gateway for reviewed workspace tools only.\n") != std::string::npos,
                "exposures inspect instructions output mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"exposures", "clear-instructions", "profile.dev"});
        require(exit_code == 0, "exposures clear-instructions exit code mismatch");
        require(harness.out.str() == "Cleared exposure profile profile.dev instructions\n",
                "exposures clear-instructions output mismatch");
        require(harness.exposure_profiles_.list_exposure_profiles().front().instructions.empty(),
                "exposures clear-instructions should clear stored value");
    }

    void test_binds_server_capabilities_to_exposure_profile() {
        CliHarness harness;
        const auto saved = harness.capabilities_.replace_for_server(
                "filesystem",
                {
                        capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                   "filesystem.read_file"),
                        capability("filesystem:prompt:summarize", mcp::app::CapabilityKind::prompt, "summarize",
                                   "filesystem.summarize"),
                });
        require(saved.has_value(), "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"exposures", "bind-server", "profile.dev", "filesystem"});

        require(exit_code == 0, "exposures bind-server exit code mismatch");
        require(harness.out.str() == "Bound 2 capability(s) from filesystem to profile.dev\n",
                "exposures bind-server output mismatch");

        const auto profiles = harness.exposure_profiles_.list_exposure_profiles();
        require(profiles.size() == 1, "bind-server exposure profile count mismatch");
        require(profiles.front().bindings.size() == 2, "bind-server binding count mismatch");
        require(profiles.front().bindings[0].exposed_name == "filesystem.read_file",
                "bind-server first exposed name mismatch");
        require(profiles.front().bindings[1].exposed_name == "filesystem.summarize",
                "bind-server second exposed name mismatch");
    }

    void test_binds_server_capabilities_to_exposure_profile_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {
                                                     capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                                "filesystem.read_file"),
                                                     capability("filesystem:prompt:summarize", mcp::app::CapabilityKind::prompt, "summarize",
                                                                "filesystem.summarize"),
                                             })
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "json exposure create failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"exposures", "bind-server", "profile.dev", "filesystem"});

        require(exit_code == 0, "json exposures bind-server exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["id"] == "profile.dev", "json exposures bind-server profile mismatch");
        require(json["boundCapabilityCount"] == 2, "json exposures bind-server count mismatch");
        require(json["bindings"].size() == 2, "json exposures bind-server bindings mismatch");
        require(json["bindings"][1]["exposedName"] == "filesystem.summarize",
                "json exposures bind-server exposed name mismatch");
    }

    void test_enables_and_disables_exposure_binding() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        const auto saved = harness.capabilities_.replace_for_server(
                "filesystem",
                {
                        capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                   "filesystem.read_file"),
                });
        require(saved.has_value(), "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"}) == 0,
                "ready exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        auto exit_code = harness.run({"exposures", "disable", "profile.dev", "filesystem:tool:read_file"});
        require(exit_code == 0, "exposures disable binding exit code mismatch");
        require(harness.out.str() == "Disabled exposure binding filesystem:tool:read_file in profile.dev\n",
                "exposures disable binding output mismatch");
        require(!harness.exposure_profiles_.list_exposure_profiles().front().bindings.front().enabled,
                "exposures disable binding should update stored binding");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"gateway", "check", "profile.dev"});
        require(exit_code == 1, "gateway check disabled binding exit code mismatch");
        require(harness.out.str().find("no enabled capability bindings configured") != std::string::npos,
                "gateway check disabled binding output mismatch");
        require(harness.out.str().find("hint: bind capabilities with: cxxmcp exposures bind-server profile.dev <server-id>") !=
                        std::string::npos,
                "gateway check disabled binding hint mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"exposures", "enable", "profile.dev", "filesystem:tool:read_file"});
        require(exit_code == 0, "exposures enable binding exit code mismatch");
        require(harness.out.str() == "Enabled exposure binding filesystem:tool:read_file in profile.dev\n",
                "exposures enable binding output mismatch");
        require(harness.exposure_profiles_.list_exposure_profiles().front().bindings.front().enabled,
                "exposures enable binding should update stored binding");
    }

    void test_enables_disables_and_unbinds_exposure_binding_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "json exposure create failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "json exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        auto exit_code = harness.run({"exposures", "disable", "profile.dev", "filesystem:tool:read_file"});
        require(exit_code == 0, "json exposures disable exit code mismatch");
        auto json = Json::parse(harness.out.str());
        require(!json["bindings"][0]["enabled"], "json exposures disable value mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"exposures", "enable", "profile.dev", "filesystem:tool:read_file"});
        require(exit_code == 0, "json exposures enable exit code mismatch");
        json = Json::parse(harness.out.str());
        require(json["bindings"][0]["enabled"], "json exposures enable value mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"exposures", "unbind", "profile.dev", "filesystem:tool:read_file"});
        require(exit_code == 0, "json exposures unbind exit code mismatch");
        json = Json::parse(harness.out.str());
        require(json["bindings"].empty(), "json exposures unbind should remove binding");
    }

    void test_prunes_stale_exposure_bindings() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"}) == 0,
                "untrusted exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "exposure bind failed");
        require(harness.capabilities_.replace_for_server("filesystem", {}).has_value(),
                "test capability removal failed");

        harness.out.str({});
        harness.out.clear();
        auto exit_code = harness.run({"gateway", "check", "profile.dev"});
        require(exit_code == 1, "gateway check stale binding exit code mismatch");
        require(harness.out.str().find("capability binding has no discovered capability") != std::string::npos,
                "gateway check stale binding issue mismatch");
        require(harness.out.str().find("hint: refresh discovery for the owning server with: cxxmcp servers discover <server-id>; or prune stale bindings with: cxxmcp exposures prune profile.dev") !=
                        std::string::npos,
                "gateway check stale binding hint mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"exposures", "prune", "profile.dev"});
        require(exit_code == 0, "exposures prune exit code mismatch");
        require(harness.out.str() == "Pruned 1 stale exposure binding(s) from profile.dev\n",
                "exposures prune output mismatch");
        require(harness.exposure_profiles_.list_exposure_profiles().front().bindings.empty(),
                "exposures prune should remove stale binding");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"exposures", "prune", "profile.dev"});
        require(exit_code == 0, "exposures prune idempotent exit code mismatch");
        require(harness.out.str() == "Pruned 0 stale exposure binding(s) from profile.dev\n",
                "exposures prune idempotent output mismatch");
    }

    void test_prunes_stale_exposure_bindings_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "json exposure create failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "json exposure bind failed");
        require(harness.capabilities_.replace_for_server("filesystem", {}).has_value(),
                "test capability removal failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"exposures", "prune", "profile.dev"});

        require(exit_code == 0, "json exposures prune exit code mismatch");
        require(harness.err.str().empty(), "json exposures prune should not write stderr");
        const auto json = Json::parse(harness.out.str());
        require(json["profileId"] == "profile.dev", "json exposures prune profile mismatch");
        require(json["prunedBindingCount"] == 1, "json exposures prune count mismatch");
        require(harness.exposure_profiles_.list_exposure_profiles().front().bindings.empty(),
                "json exposures prune should remove stale binding");
    }

    void test_inspects_unbinds_and_removes_exposure_profile() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        const auto saved = harness.capabilities_.replace_for_server(
                "filesystem",
                {
                        capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                   "filesystem.read_file"),
                });
        require(saved.has_value(), "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"}) == 0,
                "exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file", "dev.read_file"}) == 0,
                "exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        auto exit_code = harness.run({"exposures", "inspect", "profile.dev"});
        require(exit_code == 0, "exposures inspect exit code mismatch");
        require(harness.out.str().find("endpoint: http://127.0.0.1:39919/cxxmcp/dev\n") != std::string::npos,
                "exposures inspect endpoint mismatch");
        require(harness.out.str().find("readiness: ready\n") != std::string::npos,
                "exposures inspect readiness mismatch");
        require(harness.out.str().find("http-readiness: ready\n") != std::string::npos,
                "exposures inspect http readiness mismatch");
        require(harness.out.str().find("binding: profile.dev:filesystem:tool:read_file") != std::string::npos,
                "exposures inspect binding mismatch");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"exposures", "unbind", "profile.dev", "filesystem:tool:read_file"});
        require(exit_code == 0, "exposures unbind exit code mismatch");
        require(harness.out.str() == "Unbound capability filesystem:tool:read_file from profile.dev\n",
                "exposures unbind output mismatch");
        require(harness.exposure_profiles_.list_exposure_profiles().front().bindings.empty(),
                "exposures unbind should remove binding");

        harness.out.str({});
        harness.out.clear();
        exit_code = harness.run({"exposures", "remove", "profile.dev"});
        require(exit_code == 0, "exposures remove exit code mismatch");
        require(harness.out.str() == "Removed exposure profile profile.dev\n", "exposures remove output mismatch");
        require(harness.exposure_profiles_.list_exposure_profiles().empty(),
                "exposures remove should delete profile");
    }

    void test_inspects_exposure_profile_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        const auto saved = harness.capabilities_.replace_for_server(
                "filesystem",
                {
                        capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                   "filesystem.read_file"),
                });
        require(saved.has_value(), "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"}) == 0,
                "ready exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"exposures", "inspect", "profile.dev"});

        require(exit_code == 0, "json exposures inspect exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["id"] == "profile.dev", "json exposures inspect id mismatch");
        require(json["bindings"].size() == 1, "json exposures inspect bindings mismatch");
        require(json["bindings"][0]["exposedName"] == "filesystem.read_file",
                "json exposures inspect exposed name mismatch");
        require(!json["readiness"]["ready"], "json exposures inspect readiness mismatch");
        require(json["readiness"]["issues"][0]["code"] == "server_not_found",
                "json exposures inspect readiness issue mismatch");
        require(json["readiness"]["issues"][0]["suggestion"] ==
                        "import the server with: cxxmcp servers import <path>; or prune stale bindings with: cxxmcp exposures prune profile.dev",
                "json exposures inspect readiness suggestion mismatch");
        require(json.contains("gatewayStatus"), "json exposures inspect status missing");
        require(!json["gatewayStatus"]["httpReady"], "json exposures inspect http readiness mismatch");
    }

    void test_writes_gateway_client_config() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {
                                                     capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                                "filesystem.read_file"),
                                             })
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"}) == 0,
                "exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"gateway", "client-config", "profile.dev", "dev-gateway"});

        require(exit_code == 0, "gateway client-config exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["mcpServers"].contains("dev-gateway"), "gateway client-config server missing");
        require(json["mcpServers"]["dev-gateway"]["type"] == "http", "gateway client-config type mismatch");
        require(json["mcpServers"]["dev-gateway"]["url"] == "http://127.0.0.1:39919/cxxmcp/dev",
                "gateway client-config url mismatch");
    }

    void test_gateway_client_config_requires_ready_profile() {
        CliHarness harness;
        require(harness.run({"exposures", "create", "profile.empty", "Empty"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.empty", "127.0.0.1", "39919", "cxxmcp/empty"}) == 0,
                "exposure endpoint failed");

        harness.out.str({});
        harness.out.clear();
        harness.err.str({});
        harness.err.clear();
        const auto exit_code = harness.run({"gateway", "client-config", "profile.empty", "empty-gateway"});

        require(exit_code == 1, "gateway client-config should reject unready profile");
        require(harness.out.str().empty(), "gateway client-config should not write config for unready profile");
        require(harness.err.str().find("gateway profile is not ready: profile.empty\n") != std::string::npos,
                "gateway client-config readiness error mismatch");
        require(harness.err.str().find("- no enabled capability bindings configured: profile.empty") !=
                        std::string::npos,
                "gateway client-config readiness issue mismatch");
    }

    void test_writes_gateway_all_client_config() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {
                                                     capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                                "filesystem.read_file"),
                                             })
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "first exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"}) == 0,
                "first exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file", "dev.read_file"}) == 0,
                "first exposure bind failed");
        require(harness.run({"exposures", "create", "profile.ops", "Ops"}) == 0, "second exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.ops", "127.0.0.1", "39920", "/cxxmcp/ops"}) == 0,
                "second exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.ops", "filesystem:tool:read_file", "ops.read_file"}) == 0,
                "second exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"gateway", "client-config-all", "local"});

        require(exit_code == 0, "gateway client-config-all exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["mcpServers"].contains("local.profile.dev"),
                "gateway client-config-all first server missing");
        require(json["mcpServers"].contains("local.profile.ops"),
                "gateway client-config-all second server missing");
        require(json["mcpServers"]["local.profile.dev"]["url"] == "http://127.0.0.1:39919/cxxmcp/dev",
                "gateway client-config-all first url mismatch");
        require(json["mcpServers"]["local.profile.ops"]["url"] == "http://127.0.0.1:39920/cxxmcp/ops",
                "gateway client-config-all second url mismatch");
    }

    void test_gateway_all_client_config_requires_ready_profiles() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {
                                                     capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                                "filesystem.read_file"),
                                             })
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "ready exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"}) == 0,
                "ready exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "ready exposure bind failed");
        require(harness.run({"exposures", "create", "profile.empty", "Empty"}) == 0,
                "empty exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.empty", "127.0.0.1", "39920", "cxxmcp/empty"}) == 0,
                "empty exposure endpoint failed");

        harness.out.str({});
        harness.out.clear();
        harness.err.str({});
        harness.err.clear();
        const auto exit_code = harness.run({"gateway", "client-config-all", "local"});

        require(exit_code == 1, "gateway client-config-all should reject unready profiles");
        require(harness.out.str().empty(), "gateway client-config-all should not write partial config");
        require(harness.err.str().find("gateway profile is not ready: profile.empty\n") != std::string::npos,
                "gateway client-config-all readiness error mismatch");
        require(harness.err.str().find("- no enabled capability bindings configured: profile.empty") !=
                        std::string::npos,
                "gateway client-config-all readiness issue mismatch");
        require(harness.err.str().find("cxxmcp gateway client-config-all --ready-only") != std::string::npos,
                "gateway client-config-all ready-only hint mismatch");
    }

    void test_writes_gateway_ready_only_client_config() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {
                                                     capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                                "filesystem.read_file"),
                                             })
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "ready exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"}) == 0,
                "ready exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "ready exposure bind failed");
        require(harness.run({"exposures", "create", "profile.empty", "Empty"}) == 0,
                "empty exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.empty", "127.0.0.1", "39920", "cxxmcp/empty"}) == 0,
                "empty exposure endpoint failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"gateway", "client-config-all", "--ready-only", "local"});

        require(exit_code == 0, "gateway client-config-all ready-only exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["mcpServers"].contains("local.profile.dev"),
                "gateway client-config-all ready-only ready server missing");
        require(!json["mcpServers"].contains("local.profile.empty"),
                "gateway client-config-all ready-only should skip unready profile");
        require(json["mcpServers"]["local.profile.dev"]["url"] == "http://127.0.0.1:39919/cxxmcp/dev",
                "gateway client-config-all ready-only url mismatch");
    }

    void test_gateway_ready_only_client_config_skips_runtime_unready_profile() {
        CliHarness harness({tool("tool.echo", "echo", true)},
                           {profile()},
                           false,
                           failing_initialize_discovery_factory());
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {
                                                     capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                                "filesystem.read_file"),
                                             })
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "runtime exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"}) == 0,
                "runtime exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "runtime exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        harness.err.str({});
        harness.err.clear();
        const auto exit_code = harness.run({"gateway", "client-config-all", "--ready-only", "local"});

        require(exit_code == 1, "gateway ready-only client config should reject runtime-unready profiles");
        require(harness.out.str().empty(), "gateway ready-only client config should not export runtime-unready profile");
        require(harness.err.str().find("no ready HTTP gateway profiles configured") != std::string::npos,
                "gateway ready-only runtime-unready error mismatch");
    }

    void test_writes_gateway_stdio_client_config() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {
                                                     capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                                "filesystem.read_file"),
                                             })
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"gateway", "client-config-stdio", "profile.dev", "dev-gateway"});

        require(exit_code == 0, "gateway client-config-stdio exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["mcpServers"]["dev-gateway"]["command"] == "C:/bin/cxxmcp.exe",
                "gateway client-config-stdio command mismatch");
        require(json["mcpServers"]["dev-gateway"]["args"].at(0) == "--state-dir",
                "gateway client-config-stdio state-dir flag mismatch");
        require(json["mcpServers"]["dev-gateway"]["args"].at(1) == "C:/mcp-state",
                "gateway client-config-stdio state-dir value mismatch");
        require(json["mcpServers"]["dev-gateway"]["args"].at(4) == "profile.dev",
                "gateway client-config-stdio profile arg mismatch");
    }

    void test_gateway_stdio_client_config_requires_ready_bindings() {
        CliHarness harness;
        require(harness.run({"exposures", "create", "profile.empty", "Empty"}) == 0, "exposure create failed");

        harness.out.str({});
        harness.out.clear();
        harness.err.str({});
        harness.err.clear();
        const auto exit_code = harness.run({"gateway", "client-config-stdio", "profile.empty", "empty-gateway"});

        require(exit_code == 1, "gateway client-config-stdio should reject unready profile");
        require(harness.out.str().empty(), "gateway client-config-stdio should not write config for unready profile");
        require(harness.err.str().find("gateway profile is not ready: profile.empty\n") != std::string::npos,
                "gateway client-config-stdio readiness error mismatch");
        require(harness.err.str().find("- no enabled capability bindings configured: profile.empty") !=
                        std::string::npos,
                "gateway client-config-stdio readiness issue mismatch");
    }

    void test_initializes_gateway_profile_from_server() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {
                                                     capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                                "filesystem.read_file"),
                                                     capability("filesystem:prompt:summarize", mcp::app::CapabilityKind::prompt, "summarize",
                                                                "filesystem.summarize"),
                                             })
                        .has_value(),
                "test capabilities save failed");

        const auto exit_code =
                harness.run({"gateway",
                             "init",
                             "--instructions",
                             "Use reviewed workspace tools only.",
                             "profile.dev",
                             "filesystem",
                             "127.0.0.1",
                             "39919",
                             "cxxmcp/dev"});

        require(exit_code == 0, "gateway init exit code mismatch");
        require(harness.out.str().find("Initialized gateway profile profile.dev for filesystem\n") !=
                        std::string::npos,
                "gateway init output header mismatch");
        require(harness.out.str().find("Bound 2 capability(s)\n") != std::string::npos,
                "gateway init bound count mismatch");
        require(harness.out.str().find("Bindings: 2 added, 0 refreshed\n") != std::string::npos,
                "gateway init binding diff mismatch");
        require(harness.out.str().find("Endpoint: http://127.0.0.1:39919/cxxmcp/dev\n") != std::string::npos,
                "gateway init endpoint output mismatch");

        const auto profiles = harness.exposure_profiles_.list_exposure_profiles();
        require(profiles.size() == 1, "gateway init profile count mismatch");
        require(profiles.front().id == "profile.dev", "gateway init profile id mismatch");
        require(profiles.front().endpoint.listen_port == 39919, "gateway init endpoint port mismatch");
        require(profiles.front().bindings.size() == 2, "gateway init binding count mismatch");
    }

    void test_initializes_gateway_profile_from_server_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");

        const auto exit_code =
                harness.run({"gateway",
                             "init",
                             "--instructions",
                             "Use reviewed workspace tools only.",
                             "profile.dev",
                             "filesystem",
                             "127.0.0.1",
                             "39919",
                             "cxxmcp/dev"});

        require(exit_code == 0, "json gateway init exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["profileId"] == "profile.dev", "json gateway init profile id mismatch");
        require(json["serverId"] == "filesystem", "json gateway init server id mismatch");
        require(json["server"]["id"] == "filesystem", "json gateway init server object id mismatch");
        require(json["server"]["transport"] == "stdio", "json gateway init server transport mismatch");
        require(json["profile"]["id"] == "profile.dev", "json gateway init profile object id mismatch");
        require(json["profile"]["instructions"] == "Use reviewed workspace tools only.",
                "json gateway init profile instructions mismatch");
        require(json["profile"]["bindings"].size() == 1, "json gateway init profile binding count mismatch");
        require(json["created"], "json gateway init created mismatch");
        require(json["boundCapabilityCount"] == 1, "json gateway init bound count mismatch");
        require(json["addedBindingCount"] == 1, "json gateway init added binding count mismatch");
        require(json["refreshedBindingCount"] == 0, "json gateway init refreshed binding count mismatch");
        require(json["endpoint"]["url"] == "http://127.0.0.1:39919/cxxmcp/dev",
                "json gateway init endpoint mismatch");
        require(json["readiness"]["ready"], "json gateway init readiness mismatch");
    }

    void test_gateway_init_reuses_existing_profile() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code =
                harness.run({"gateway", "init", "profile.dev", "filesystem", "127.0.0.1", "39920", "cxxmcp/updated"});

        require(exit_code == 0, "gateway init existing profile exit code mismatch");
        require(harness.out.str().find("Profile: reused\n") != std::string::npos,
                "gateway init existing profile status mismatch");
        require(harness.out.str().find("Bindings: 1 added, 0 refreshed\n") != std::string::npos,
                "gateway init existing profile binding diff mismatch");
        const auto profiles = harness.exposure_profiles_.list_exposure_profiles();
        require(profiles.size() == 1, "gateway init existing profile should not duplicate profile");
        require(profiles.front().endpoint.listen_port == 39920, "gateway init existing profile endpoint mismatch");
        require(profiles.front().endpoint.path == "/cxxmcp/updated", "gateway init existing profile path mismatch");
        require(profiles.front().bindings.size() == 1, "gateway init existing profile binding count mismatch");
    }

    void test_gateway_init_refreshes_existing_bindings() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"}) == 0,
                "untrusted exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code =
                harness.run({"gateway", "init", "profile.dev", "filesystem", "127.0.0.1", "39920", "cxxmcp/updated"});

        require(exit_code == 0, "gateway init refreshed binding exit code mismatch");
        require(harness.out.str().find("Profile: reused\n") != std::string::npos,
                "gateway init refreshed binding profile status mismatch");
        require(harness.out.str().find("Bindings: 0 added, 1 refreshed\n") != std::string::npos,
                "gateway init refreshed binding diff mismatch");
        const auto profiles = harness.exposure_profiles_.list_exposure_profiles();
        require(profiles.size() == 1, "gateway init refreshed binding profile count mismatch");
        require(profiles.front().bindings.size() == 1, "gateway init refreshed binding should not duplicate");
    }

    void test_gateway_init_refreshes_existing_bindings_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code =
                harness.run({"gateway", "init", "profile.dev", "filesystem", "127.0.0.1", "39920", "cxxmcp/updated"});

        require(exit_code == 0, "json gateway init refreshed binding exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(!json["created"], "json gateway init refreshed binding created mismatch");
        require(json["boundCapabilityCount"] == 1, "json gateway init refreshed binding bound count mismatch");
        require(json["addedBindingCount"] == 0, "json gateway init refreshed binding added count mismatch");
        require(json["refreshedBindingCount"] == 1, "json gateway init refreshed binding refreshed count mismatch");
        require(harness.exposure_profiles_.list_exposure_profiles().front().bindings.size() == 1,
                "json gateway init refreshed binding should not duplicate");
    }

    void test_gateway_init_without_capabilities_reports_discovery_hint() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");

        const auto exit_code =
                harness.run({"gateway", "init", "profile.dev", "filesystem", "127.0.0.1", "39919", "cxxmcp/dev"});

        require(exit_code == 1, "gateway init missing capabilities exit code mismatch");
        require(harness.err.str().find("no capabilities discovered for server: filesystem") != std::string::npos,
                "gateway init missing capabilities error mismatch");
        require(harness.err.str().find("hint: discover capabilities with: cxxmcp servers discover filesystem") !=
                        std::string::npos,
                "gateway init missing capabilities hint mismatch");
        require(harness.exposure_profiles_.list_exposure_profiles().empty(),
                "gateway init missing capabilities should not create profile");
    }

    void test_initializes_gateway_profile_with_discovery() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");

        const auto exit_code =
                harness.run({"gateway", "init", "--discover", "profile.dev", "filesystem", "127.0.0.1", "39919", "cxxmcp/dev"});

        require(exit_code == 0, "gateway init discover exit code mismatch");
        require(harness.out.str().find("Discovered 3 capability(s)\n") != std::string::npos,
                "gateway init discover output mismatch");
        require(harness.out.str().find("Bound 3 capability(s)\n") != std::string::npos,
                "gateway init discover bound count mismatch");
        require(harness.capabilities_.list_capabilities().size() == 3,
                "gateway init discover should persist discovered capabilities");
        require(harness.exposure_profiles_.list_exposure_profiles().front().bindings.size() == 3,
                "gateway init discover binding count mismatch");
    }

    void test_initializes_gateway_profile_with_stdio_server_onboarding() {
        CliHarness harness;

        const auto exit_code = harness.run({"gateway",
                                            "init-stdio",
                                            "--trust",
                                            "--discover",
                                            "--path",
                                            "cxxmcp/child",
                                            "profile.child",
                                            "child",
                                            "127.0.0.1",
                                            "39941",
                                            "C:/bin/child.exe",
                                            "--workspace",
                                            "C:/workspace"});

        require(exit_code == 0, "gateway init-stdio exit code mismatch");
        require(harness.out.str().find("Initialized gateway profile profile.child for child\n") !=
                        std::string::npos,
                "gateway init-stdio output header mismatch");
        require(harness.out.str().find("Trust: trusted\n") != std::string::npos,
                "gateway init-stdio trust output mismatch");
        require(harness.out.str().find("Discovered 3 capability(s)\n") != std::string::npos,
                "gateway init-stdio discovery output mismatch");
        require(harness.out.str().find("Endpoint: http://127.0.0.1:39941/cxxmcp/child\n") !=
                        std::string::npos,
                "gateway init-stdio endpoint output mismatch");

        const auto servers = harness.servers_.list_servers();
        require(servers.size() == 1, "gateway init-stdio server count mismatch");
        require(servers.front().id == "child", "gateway init-stdio server id mismatch");
        require(servers.front().stdio.command == "C:/bin/child.exe", "gateway init-stdio command mismatch");
        require(servers.front().stdio.args.size() == 2, "gateway init-stdio args count mismatch");
        require(servers.front().stdio.args[0] == "--workspace", "gateway init-stdio first arg mismatch");
        require(servers.front().trust == mcp::app::McpServerTrustState::trusted,
                "gateway init-stdio should trust server when requested");
        require(harness.exposure_profiles_.list_exposure_profiles().front().bindings.size() == 3,
                "gateway init-stdio binding count mismatch");
    }

    void test_initializes_gateway_profile_with_stdio_server_onboarding_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);

        const auto exit_code = harness.run({"gateway",
                                            "init-stdio",
                                            "--trust",
                                            "--discover",
                                            "--path",
                                            "/cxxmcp/child",
                                            "--instructions",
                                            "Use child tools only.",
                                            "profile.child",
                                            "child",
                                            "127.0.0.1",
                                            "39941",
                                            "C:/bin/child.exe"});

        require(exit_code == 0, "json gateway init-stdio exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["profileId"] == "profile.child", "json gateway init-stdio profile mismatch");
        require(json["serverId"] == "child", "json gateway init-stdio server mismatch");
        require(json["created"], "json gateway init-stdio created mismatch");
        require(json["trusted"], "json gateway init-stdio trusted mismatch");
        require(json["discovered"], "json gateway init-stdio discovered mismatch");
        require(json["discoveredCapabilityCount"] == 3, "json gateway init-stdio discovery count mismatch");
        require(json["boundCapabilityCount"] == 3, "json gateway init-stdio bound count mismatch");
        require(json["endpoint"]["url"] == "http://127.0.0.1:39941/cxxmcp/child",
                "json gateway init-stdio endpoint mismatch");
        require(json["readiness"]["ready"], "json gateway init-stdio readiness mismatch");
        require(json["server"]["id"] == "child", "json gateway init-stdio server object id mismatch");
        require(json["server"]["transport"] == "stdio", "json gateway init-stdio server transport mismatch");
        require(json["server"]["stdio"]["command"] == "C:/bin/child.exe",
                "json gateway init-stdio server command mismatch");
        require(json["profile"]["id"] == "profile.child", "json gateway init-stdio profile object id mismatch");
        require(json["profile"]["instructions"] == "Use child tools only.",
                "json gateway init-stdio profile instructions mismatch");
        require(json["profile"]["bindings"].size() == 3, "json gateway init-stdio profile binding count mismatch");
        require(harness.servers_.list_servers().front().stdio.command == "C:/bin/child.exe",
                "json gateway init-stdio command mismatch");
    }

    void test_initializes_gateway_profile_with_http_server_onboarding() {
        CliHarness harness;

        const auto exit_code = harness.run({"gateway",
                                            "init-http",
                                            "--trust",
                                            "--discover",
                                            "--path",
                                            "cxxmcp/remote",
                                            "--header",
                                            "Authorization",
                                            "Bearer token",
                                            "profile.remote",
                                            "remote",
                                            "127.0.0.1",
                                            "39942",
                                            "http://127.0.0.1:3000/mcp"});

        require(exit_code == 0, "gateway init-http exit code mismatch");
        require(harness.out.str().find("Initialized gateway profile profile.remote for remote\n") !=
                        std::string::npos,
                "gateway init-http output header mismatch");
        require(harness.out.str().find("Trust: trusted\n") != std::string::npos,
                "gateway init-http trust output mismatch");
        require(harness.out.str().find("Discovered 3 capability(s)\n") != std::string::npos,
                "gateway init-http discovery output mismatch");
        require(harness.out.str().find("Endpoint: http://127.0.0.1:39942/cxxmcp/remote\n") !=
                        std::string::npos,
                "gateway init-http endpoint output mismatch");

        const auto servers = harness.servers_.list_servers();
        require(servers.size() == 1, "gateway init-http server count mismatch");
        require(servers.front().id == "remote", "gateway init-http server id mismatch");
        require(servers.front().transport == mcp::app::McpServerTransportKind::streamable_http,
                "gateway init-http transport mismatch");
        require(servers.front().http.url == "http://127.0.0.1:3000/mcp", "gateway init-http url mismatch");
        require(servers.front().http.headers.at("Authorization") == "Bearer token",
                "gateway init-http header mismatch");
        require(servers.front().trust == mcp::app::McpServerTrustState::trusted,
                "gateway init-http should trust server when requested");
        require(harness.exposure_profiles_.list_exposure_profiles().front().bindings.size() == 3,
                "gateway init-http binding count mismatch");
    }

    void test_initializes_gateway_profile_with_http_server_onboarding_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);

        const auto exit_code = harness.run({"gateway",
                                            "init-http",
                                            "--trust",
                                            "--discover",
                                            "--path",
                                            "/cxxmcp/remote",
                                            "--instructions",
                                            "Use remote tools only.",
                                            "--header",
                                            "Authorization",
                                            "Bearer token",
                                            "profile.remote",
                                            "remote",
                                            "127.0.0.1",
                                            "39942",
                                            "http://127.0.0.1:3000/mcp"});

        require(exit_code == 0, "json gateway init-http exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["profileId"] == "profile.remote", "json gateway init-http profile mismatch");
        require(json["serverId"] == "remote", "json gateway init-http server mismatch");
        require(json["created"], "json gateway init-http created mismatch");
        require(json["trusted"], "json gateway init-http trusted mismatch");
        require(json["discovered"], "json gateway init-http discovered mismatch");
        require(json["discoveredCapabilityCount"] == 3, "json gateway init-http discovery count mismatch");
        require(json["boundCapabilityCount"] == 3, "json gateway init-http bound count mismatch");
        require(json["endpoint"]["url"] == "http://127.0.0.1:39942/cxxmcp/remote",
                "json gateway init-http endpoint mismatch");
        require(json["readiness"]["ready"], "json gateway init-http readiness mismatch");
        require(json["server"]["id"] == "remote", "json gateway init-http server object id mismatch");
        require(json["server"]["transport"] == "streamable_http",
                "json gateway init-http server transport mismatch");
        require(json["server"]["http"]["headers"]["Authorization"] == "Bearer token",
                "json gateway init-http server header mismatch");
        require(json["profile"]["id"] == "profile.remote", "json gateway init-http profile object id mismatch");
        require(json["profile"]["instructions"] == "Use remote tools only.",
                "json gateway init-http profile instructions mismatch");
        require(json["profile"]["bindings"].size() == 3, "json gateway init-http profile binding count mismatch");
        const auto server = harness.servers_.list_servers().front();
        require(server.http.url == "http://127.0.0.1:3000/mcp", "json gateway init-http url mismatch");
        require(server.http.headers.at("Authorization") == "Bearer token",
                "json gateway init-http header mismatch");
    }

    void test_gateway_init_discover_reports_trust_hint() {
        CliHarness harness;
        auto server = mcp_server();
        server.trust = mcp::app::McpServerTrustState::untrusted;
        require(harness.servers_.save(server).has_value(), "test untrusted cxxmcp server save failed");

        const auto exit_code =
                harness.run({"gateway", "init", "--discover", "profile.dev", "filesystem", "127.0.0.1", "39919", "cxxmcp/dev"});

        require(exit_code == 1, "gateway init discover untrusted exit code mismatch");
        require(harness.err.str().find("cxxmcp server is untrusted: filesystem") != std::string::npos,
                "gateway init discover untrusted error mismatch");
        require(harness.err.str().find("hint: trust the server with: cxxmcp servers trust filesystem") !=
                        std::string::npos,
                "gateway init discover untrusted hint mismatch");
        require(harness.exposure_profiles_.list_exposure_profiles().empty(),
                "gateway init discover should not create profile on discovery failure");
    }

    void test_gateway_init_trusts_and_discovers_server() {
        CliHarness harness;
        auto server = mcp_server();
        server.trust = mcp::app::McpServerTrustState::untrusted;
        require(harness.servers_.save(server).has_value(), "test untrusted cxxmcp server save failed");

        const auto exit_code =
                harness.run({"gateway", "init", "--trust", "--discover", "profile.dev", "filesystem", "127.0.0.1", "39919",
                             "cxxmcp/dev"});

        require(exit_code == 0, "gateway init trust discover exit code mismatch");
        require(harness.out.str().find("Trust: trusted\n") != std::string::npos,
                "gateway init trust output mismatch");
        require(harness.out.str().find("Discovered 3 capability(s)\n") != std::string::npos,
                "gateway init trust discover output mismatch");
        const auto updated = harness.server_management_.get_server("filesystem");
        require(updated.has_value(), "gateway init trust updated server lookup failed");
        require(updated->trust == mcp::app::McpServerTrustState::trusted,
                "gateway init trust should update server trust");
        require(harness.exposure_profiles_.list_exposure_profiles().front().bindings.size() == 3,
                "gateway init trust discover binding count mismatch");
    }

    void test_initializes_gateway_profile_with_discovery_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");

        const auto exit_code =
                harness.run({"gateway", "init", "--discover", "profile.dev", "filesystem", "127.0.0.1", "39919", "cxxmcp/dev"});

        require(exit_code == 0, "json gateway init discover exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["discovered"], "json gateway init discover flag mismatch");
        require(json["discoveredCapabilityCount"] == 3, "json gateway init discover count mismatch");
        require(json["boundCapabilityCount"] == 3, "json gateway init discover bound count mismatch");
        require(json["readiness"]["ready"], "json gateway init discover readiness mismatch");
    }

    void test_gateway_init_trusts_and_discovers_server_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        auto server = mcp_server();
        server.trust = mcp::app::McpServerTrustState::untrusted;
        require(harness.servers_.save(server).has_value(), "test untrusted cxxmcp server save failed");

        const auto exit_code =
                harness.run({"gateway", "init", "--discover", "--trust", "profile.dev", "filesystem", "127.0.0.1", "39919",
                             "cxxmcp/dev"});

        require(exit_code == 0, "json gateway init trust discover exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["trusted"], "json gateway init trust flag mismatch");
        require(json["discovered"], "json gateway init trust discover flag mismatch");
        require(json["discoveredCapabilityCount"] == 3, "json gateway init trust discover count mismatch");
        require(json["readiness"]["ready"], "json gateway init trust discover readiness mismatch");
    }

    void test_gateway_onboarding_one_shot_flow() {
        CliHarness harness;
        auto server = mcp_server();
        server.trust = mcp::app::McpServerTrustState::untrusted;
        require(harness.servers_.save(server).has_value(), "test untrusted cxxmcp server save failed");

        const auto init_exit =
                harness.run({"gateway", "init", "--trust", "--discover", "profile.dev", "filesystem", "127.0.0.1", "39931",
                             "/cxxmcp/cli"});

        require(init_exit == 0, "onboarding gateway init exit code mismatch");
        require(harness.out.str().find("Readiness: ready\n") != std::string::npos,
                "onboarding gateway init readiness output mismatch");
        require(harness.out.str().find("Next: cxxmcp gateway check profile.dev\n") != std::string::npos,
                "onboarding gateway init next check output mismatch");
        require(harness.out.str().find("Next: cxxmcp gateway client-config profile.dev\n") != std::string::npos,
                "onboarding gateway init next client-config output mismatch");
        require(harness.out.str().find("Next: cxxmcp gateway serve-http profile.dev\n") != std::string::npos,
                "onboarding gateway init next serve-http output mismatch");
        require(harness.out.str().find("Next: cxxmcp gateway client-config-stdio profile.dev\n") != std::string::npos,
                "onboarding gateway init next stdio client-config output mismatch");

        harness.out.str({});
        harness.out.clear();
        require(harness.run({"gateway", "check", "profile.dev"}) == 0, "onboarding gateway check failed");
        require(harness.out.str() == "profile.dev\tready\t3 enabled binding(s)\n",
                "onboarding gateway check output mismatch");

        harness.out.str({});
        harness.out.clear();
        require(harness.run({"gateway", "client-config", "profile.dev", "dev-gateway"}) == 0,
                "onboarding gateway client-config failed");
        const auto config = Json::parse(harness.out.str());
        require(config["mcpServers"]["dev-gateway"]["url"] == "http://127.0.0.1:39931/cxxmcp/cli",
                "onboarding gateway client-config url mismatch");

        harness.out.str({});
        harness.out.clear();
        require(harness.run({"gateway", "preview", "profile.dev"}) == 0, "onboarding gateway preview failed");
        require(harness.out.str().find("tools:\n- filesystem.read_file\tRead a file\n") != std::string::npos,
                "onboarding gateway preview tool mismatch");
        require(harness.out.str().find("prompts:\n- filesystem.summarize\tSummarize text\n") != std::string::npos,
                "onboarding gateway preview prompt mismatch");
        require(harness.out.str().find("resources:\n- file:///tmp/readme.md\tfilesystem.readme\tReadme file\n") !=
                        std::string::npos,
                "onboarding gateway preview resource mismatch");
    }

    void test_gateway_init_all_initializes_discovered_servers_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.servers_.save(mcp_server("empty", "empty")).has_value(),
                "test empty cxxmcp server save failed");
        auto untrusted_server = mcp_server("untrusted", "untrusted");
        untrusted_server.trust = mcp::app::McpServerTrustState::untrusted;
        require(harness.servers_.save(untrusted_server).has_value(),
                "test untrusted cxxmcp server save failed");
        auto untrusted_capability =
                capability("untrusted:tool:read_secret", mcp::app::CapabilityKind::tool, "read_secret",
                           "untrusted.read_secret");
        untrusted_capability.server_id = "untrusted";
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.capabilities_.replace_for_server("untrusted", {untrusted_capability}).has_value(),
                "test untrusted capability save failed");

        const auto exit_code = harness.run({"gateway",
                                            "init-all",
                                            "--instructions",
                                            "Use imported tools only.",
                                            "127.0.0.1",
                                            "39950",
                                            "cxxmcp/imported"});

        require(exit_code == 0, "json gateway init-all exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["initializedCount"] == 1, "json gateway init-all initialized count mismatch");
        require(json["skippedCount"] == 2, "json gateway init-all skipped count mismatch");
        require(json["profiles"].size() == 3, "json gateway init-all report count mismatch");
        require(json["profiles"][0]["profileId"] == "profile.filesystem",
                "json gateway init-all profile id mismatch");
        require(json["profiles"][0]["url"] == "http://127.0.0.1:39950/cxxmcp/imported/filesystem",
                "json gateway init-all url mismatch");
        require(json["profiles"][0]["boundCapabilityCount"] == 1,
                "json gateway init-all binding count mismatch");
        require(json["profiles"][1]["serverId"] == "empty", "json gateway init-all skipped server mismatch");
        require(json["profiles"][1]["skippedReason"] == "no capabilities discovered for server",
                "json gateway init-all skipped reason mismatch");
        require(json["profiles"][2]["serverId"] == "untrusted",
                "json gateway init-all untrusted server mismatch");
        require(json["profiles"][2]["skippedReason"] == "cxxmcp server is untrusted",
                "json gateway init-all untrusted reason mismatch");

        const auto profile = harness.exposure_management_.get_profile("profile.filesystem");
        require(profile.has_value(), "gateway init-all should save profile");
        require(profile->endpoint.listen_port == 39950, "gateway init-all saved endpoint port mismatch");
        require(profile->endpoint.path == "/cxxmcp/imported/filesystem",
                "gateway init-all saved endpoint path mismatch");
        require(profile->instructions == "Use imported tools only.",
                "gateway init-all saved instructions mismatch");
        require(profile->bindings.size() == 1, "gateway init-all saved binding count mismatch");
    }

    void test_gateway_init_all_prints_ready_only_next_steps() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");

        const auto exit_code = harness.run({"gateway", "init-all", "127.0.0.1", "39950", "cxxmcp/imported"});

        require(exit_code == 0, "gateway init-all next steps exit code mismatch");
        require(harness.out.str().find("Next: cxxmcp gateway status\n") != std::string::npos,
                "gateway init-all next status missing");
        require(harness.out.str().find("Next: cxxmcp gateway client-config-all --ready-only\n") !=
                        std::string::npos,
                "gateway init-all next ready-only client config missing");
        require(harness.out.str().find("Next: cxxmcp gateway serve-all --ready-only\n") !=
                        std::string::npos,
                "gateway init-all next ready-only serve missing");
    }

    void test_gateway_init_all_trusts_and_discovers_servers_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        auto server = mcp_server();
        server.trust = mcp::app::McpServerTrustState::untrusted;
        require(harness.servers_.save(server).has_value(), "test untrusted cxxmcp server save failed");

        const auto exit_code = harness.run({"gateway",
                                            "init-all",
                                            "--trust",
                                            "--discover",
                                            "--instructions",
                                            "Use discovered tools only.",
                                            "127.0.0.1",
                                            "39960",
                                            "/cxxmcp/discovered"});

        require(exit_code == 0, "json gateway init-all trust discover exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["trusted"], "json gateway init-all trust flag mismatch");
        require(json["trustedServerCount"] == 1, "json gateway init-all trusted count mismatch");
        require(json["discovered"], "json gateway init-all discover flag mismatch");
        require(json["discoveredCapabilityCount"] == 3,
                "json gateway init-all discovered count mismatch");
        require(json["discoveryReports"].size() == 1,
                "json gateway init-all discovery report count mismatch");
        require(json["initializedCount"] == 1, "json gateway init-all trust discover initialized count mismatch");
        require(json["skippedCount"] == 0, "json gateway init-all trust discover skipped count mismatch");
        require(json["profiles"][0]["profileId"] == "profile.filesystem",
                "json gateway init-all trust discover profile id mismatch");

        const auto profile = harness.exposure_management_.get_profile("profile.filesystem");
        require(profile.has_value(), "gateway init-all trust discover should save profile");
        require(profile->bindings.size() == 3, "gateway init-all trust discover binding count mismatch");
        require(profile->instructions == "Use discovered tools only.",
                "gateway init-all trust discover instructions mismatch");
    }

    void test_gateway_import_config_trusts_discovers_and_initializes_as_json() {
        const auto path = std::filesystem::temp_directory_path() / "mcp-cli-gateway-import-config-test.json";
        std::error_code ec;
        std::filesystem::remove(path, ec);

        {
            std::ofstream output(path);
            output << R"({
  "mcpServers": {
    "filesystem": {
      "command": "node",
      "args": ["server.js"]
    }
  }
})";
        }

        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        const auto exit_code = harness.run({"gateway",
                                            "import-config",
                                            "--trust",
                                            "--discover",
                                            "--profile-prefix",
                                            "gateway.",
                                            "--instructions",
                                            "Use imported config tools.",
                                            path.string(),
                                            "127.0.0.1",
                                            "39970",
                                            "/cxxmcp/imported"});

        require(exit_code == 0, "json gateway import-config exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["importedCount"] == 1, "json gateway import-config imported count mismatch");
        require(json["trusted"], "json gateway import-config trust flag mismatch");
        require(json["trustedServerCount"] == 1, "json gateway import-config trusted count mismatch");
        require(json["discovered"], "json gateway import-config discover flag mismatch");
        require(json["discoveredCapabilityCount"] == 3,
                "json gateway import-config discovered count mismatch");
        require(json["initializedCount"] == 1, "json gateway import-config initialized count mismatch");
        require(json["skippedCount"] == 0, "json gateway import-config skipped count mismatch");
        require(json["profiles"][0]["profileId"] == "gateway.filesystem",
                "json gateway import-config profile id mismatch");
        require(json["profiles"][0]["url"] == "http://127.0.0.1:39970/cxxmcp/imported/filesystem",
                "json gateway import-config endpoint url mismatch");

        const auto profile = harness.exposure_management_.get_profile("gateway.filesystem");
        require(profile.has_value(), "gateway import-config should save profile");
        require(profile->bindings.size() == 3, "gateway import-config binding count mismatch");
        require(profile->instructions == "Use imported config tools.",
                "gateway import-config instructions mismatch");

        std::filesystem::remove(path, ec);
    }

    void test_checks_ready_gateway_profile() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"}) == 0,
                "ready exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"gateway", "check", "profile.dev"});

        require(exit_code == 0, "gateway check ready exit code mismatch");
        require(harness.out.str() == "profile.dev\tready\t1 enabled binding(s)\n",
                "gateway check ready output mismatch");
    }

    void test_gateway_check_requires_http_endpoint() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"gateway", "check", "profile.dev"});

        require(exit_code == 1, "gateway check endpointless profile exit code mismatch");
        require(harness.out.str().find("profile.dev\tnot-ready\t1 enabled binding(s)\n") != std::string::npos,
                "gateway check endpointless header mismatch");
        require(harness.out.str().find("gateway HTTP endpoint is not configured") != std::string::npos,
                "gateway check endpointless issue mismatch");
    }

    void test_checks_gateway_profile_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        auto server = mcp_server();
        server.trust = mcp::app::McpServerTrustState::untrusted;
        require(harness.servers_.save(server).has_value(), "test untrusted server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"}) == 0,
                "untrusted exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"gateway", "check", "profile.dev"});

        require(exit_code == 1, "json gateway check unready exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(!json["ready"], "json gateway check ready mismatch");
        require(!json["httpReady"], "json gateway check http ready mismatch");
        require(json["issues"].size() == 1, "json gateway check issue count mismatch");
        require(json["issues"][0]["code"] == "server_untrusted", "json gateway check issue code mismatch");
        require(json["issues"][0]["suggestion"] == "trust the server with: cxxmcp servers trust filesystem",
                "json gateway check suggestion mismatch");
    }

    void test_gateway_check_reports_runtime_unready_upstream_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)},
                           {profile()},
                           true,
                           failing_initialize_discovery_factory());
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"}) == 0,
                "runtime-unready exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"gateway", "check", "profile.dev"});

        require(exit_code == 1, "json gateway check runtime-unready exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(!json["ready"], "json gateway check runtime-unready ready mismatch");
        require(!json["httpReady"], "json gateway check runtime-unready http ready mismatch");
        require(json["issues"].size() == 1, "json gateway check runtime-unready issue count mismatch");
        require(json["issues"][0]["code"] == "server_unready",
                "json gateway check runtime-unready issue code mismatch");
        require(json["issues"][0]["message"] == "upstream initialize failed",
                "json gateway check runtime-unready issue message mismatch");
        require(json["issues"][0]["suggestion"] == "check upstream health with: cxxmcp servers check filesystem",
                "json gateway check runtime-unready suggestion mismatch");
    }

    void test_gateway_status_reports_runtime_unready_upstream_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)},
                           {profile()},
                           true,
                           failing_initialize_discovery_factory());
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "status exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"}) == 0,
                "status exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "status exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"gateway", "status"});

        require(exit_code == 1, "json gateway status runtime-unready exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(!json["ready"], "json gateway status runtime-unready ready mismatch");
        require(json["profileCount"] == 1, "json gateway status runtime-unready profile count mismatch");
        require(json["readyProfileCount"] == 0, "json gateway status runtime-unready ready count mismatch");
        require(json["profiles"][0]["profileId"] == "profile.dev",
                "json gateway status runtime-unready profile id mismatch");
        require(!json["profiles"][0]["ready"], "json gateway status runtime-unready profile readiness mismatch");
        require(json["profiles"][0]["issues"].size() == 1,
                "json gateway status runtime-unready profile issue count mismatch");
        require(json["profiles"][0]["issues"][0]["code"] == "server_unready",
                "json gateway status runtime-unready profile issue code mismatch");
    }

    void test_checks_all_gateway_profiles() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.ready", "Ready"}) == 0, "ready exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.ready", "127.0.0.1", "39919", "cxxmcp/ready"}) == 0,
                "ready exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.ready", "filesystem:tool:read_file"}) == 0,
                "ready exposure bind failed");
        require(harness.run({"exposures", "create", "profile.empty", "Empty"}) == 0, "empty exposure create failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"gateway", "check-all"});

        require(exit_code == 1, "gateway check-all mixed readiness exit code mismatch");
        require(harness.out.str().find("profile.ready\tready\t1 enabled binding(s)\n") != std::string::npos,
                "gateway check-all ready profile output mismatch");
        require(harness.out.str().find("profile.empty\tnot-ready\t0 enabled binding(s)\n") != std::string::npos,
                "gateway check-all not-ready profile output mismatch");
        require(harness.out.str().find("- no enabled capability bindings configured: profile.empty\n") !=
                        std::string::npos,
                "gateway check-all issue output mismatch");
    }

    void test_checks_all_gateway_profiles_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.ready", "Ready"}) == 0, "json ready exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.ready", "127.0.0.1", "39919", "cxxmcp/ready"}) == 0,
                "json ready exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.ready", "filesystem:tool:read_file"}) == 0,
                "json ready exposure bind failed");
        require(harness.run({"exposures", "create", "profile.empty", "Empty"}) == 0, "json empty exposure create failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"gateway", "check-all"});

        require(exit_code == 1, "json gateway check-all mixed readiness exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(!json["ready"], "json gateway check-all ready mismatch");
        require(json["profileCount"] == 2, "json gateway check-all profile count mismatch");
        require(json["profiles"][0]["profileId"] == "profile.ready", "json gateway check-all first profile mismatch");
        require(json["profiles"][0]["ready"], "json gateway check-all first readiness mismatch");
        require(json["profiles"][0]["httpReady"], "json gateway check-all first http readiness mismatch");
        require(json["profiles"][1]["profileId"] == "profile.empty", "json gateway check-all second profile mismatch");
        require(!json["profiles"][1]["ready"], "json gateway check-all second readiness mismatch");
        require(!json["profiles"][1]["httpReady"], "json gateway check-all second http readiness mismatch");
        require(json["profiles"][1]["issues"][0]["code"] == "no_enabled_bindings",
                "json gateway check-all binding issue code mismatch");
        require(json["profiles"][1]["issues"][1]["code"] == "endpoint_not_configured",
                "json gateway check-all endpoint issue code mismatch");
    }

    void test_gateway_status_summarizes_http_client_readiness() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.ready", "Ready"}) == 0,
                "ready exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.ready", "127.0.0.1", "39919", "cxxmcp/ready"}) == 0,
                "ready exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.ready", "filesystem:tool:read_file"}) == 0,
                "ready exposure bind failed");
        require(harness.run({"exposures", "create", "profile.no-endpoint", "No Endpoint"}) == 0,
                "endpointless exposure create failed");
        require(harness.run({"exposures", "bind", "profile.no-endpoint", "filesystem:tool:read_file"}) == 0,
                "endpointless exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"gateway", "status"});

        require(exit_code == 1, "gateway status mixed readiness exit code mismatch");
        require(harness.out.str().find("gateway\tnot-ready\n") != std::string::npos,
                "gateway status header mismatch");
        require(harness.out.str().find("profiles\t1/2 ready for HTTP clients\n") != std::string::npos,
                "gateway status count mismatch");
        require(harness.out.str().find("profile.ready\tready\thttp://127.0.0.1:39919/cxxmcp/ready\t1 enabled binding(s)\n") !=
                        std::string::npos,
                "gateway status ready profile mismatch");
        require(harness.out.str().find("profile.no-endpoint\tnot-ready\t<endpoint not configured>\t1 enabled binding(s)\n") !=
                        std::string::npos,
                "gateway status endpointless profile mismatch");
        require(harness.out.str().find("- gateway HTTP endpoint is not configured\n") != std::string::npos,
                "gateway status endpoint issue missing");
        require(harness.out.str().find("Next: cxxmcp gateway client-config-all --ready-only\n") !=
                        std::string::npos,
                "gateway status next client config missing");
        require(harness.out.str().find("Next: cxxmcp gateway serve-all --ready-only\n") != std::string::npos,
                "gateway status next serve missing");
    }

    void test_gateway_status_reports_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        require(harness.run({"gateway", "status"}) == 1, "json gateway status empty exit code mismatch");
        auto json = Json::parse(harness.out.str());
        require(!json["ready"], "json gateway status empty ready mismatch");
        require(json["profileCount"] == 0, "json gateway status empty profile count mismatch");
        require(json["issues"][0]["code"] == "no_exposure_profiles",
                "json gateway status empty issue mismatch");

        harness.out.str({});
        harness.out.clear();
        require(harness.servers_.save(mcp_server()).has_value(), "json test cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "json test capability save failed");
        require(harness.run({"exposures", "create", "profile.ready", "Ready"}) == 0,
                "json ready exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.ready", "127.0.0.1", "39919", "cxxmcp/ready"}) == 0,
                "json ready exposure endpoint failed");
        require(harness.run({"exposures", "bind", "profile.ready", "filesystem:tool:read_file"}) == 0,
                "json ready exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"gateway", "status"});

        require(exit_code == 0, "json gateway status ready exit code mismatch");
        json = Json::parse(harness.out.str());
        require(json["ready"], "json gateway status ready mismatch");
        require(json["readyProfileCount"] == 1, "json gateway status ready count mismatch");
        require(json["profiles"][0]["httpReady"], "json gateway status profile http ready mismatch");
        require(json["profiles"][0]["endpointConfigured"],
                "json gateway status endpoint configured mismatch");
        require(json["profiles"][0]["endpoint"]["url"] == "http://127.0.0.1:39919/cxxmcp/ready",
                "json gateway status endpoint url mismatch");
    }

    void test_previews_gateway_exposed_capabilities() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        auto read_file = capability("filesystem:tool:read_file",
                                    mcp::app::CapabilityKind::tool,
                                    "read_file",
                                    "filesystem.read_file");
        read_file.description = "Read a file";
        auto summarize = capability("filesystem:prompt:summarize",
                                    mcp::app::CapabilityKind::prompt,
                                    "summarize",
                                    "filesystem.summarize");
        summarize.description = "Summarize text";
        auto readme = capability("filesystem:resource:file:///tmp/readme.md",
                                 mcp::app::CapabilityKind::resource,
                                 "readme",
                                 "filesystem.readme");
        readme.description = "Readme file";
        readme.uri = "file:///tmp/readme.md";
        readme.output_schema = Json{{"mimeType", "text/markdown"}};
        require(harness.capabilities_.replace_for_server("filesystem", {read_file, summarize, readme}).has_value(),
                "test capabilities save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "endpoint", "profile.dev", "127.0.0.1", "39919", "cxxmcp/dev"}) == 0,
                "exposure endpoint failed");
        require(harness.run({"exposures", "bind-server", "profile.dev", "filesystem"}) == 0,
                "exposure bind-server failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"gateway", "preview", "profile.dev"});

        require(exit_code == 0, "gateway preview exit code mismatch");
        require(harness.out.str().find("profile.dev\tready\thttp://127.0.0.1:39919/cxxmcp/dev\n") !=
                        std::string::npos,
                "gateway preview header mismatch");
        require(harness.out.str().find("tools:\n- filesystem.read_file\tRead a file\n") != std::string::npos,
                "gateway preview tools mismatch");
        require(harness.out.str().find("prompts:\n- filesystem.summarize\tSummarize text\n") != std::string::npos,
                "gateway preview prompts mismatch");
        require(harness.out.str().find("resources:\n- file:///tmp/readme.md\tfilesystem.readme\tReadme file\n") !=
                        std::string::npos,
                "gateway preview resources mismatch");
    }

    void test_previews_gateway_exposed_capabilities_as_json() {
        CliHarness harness({tool("tool.echo", "echo", true)}, {profile()}, true);
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        auto read_file = capability("filesystem:tool:read_file",
                                    mcp::app::CapabilityKind::tool,
                                    "read_file",
                                    "filesystem.read_file");
        read_file.description = "Read a file";
        require(harness.capabilities_.replace_for_server("filesystem", {read_file}).has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"gateway", "preview", "profile.dev"});

        require(exit_code == 0, "json gateway preview exit code mismatch");
        const auto json = Json::parse(harness.out.str());
        require(json["profileId"] == "profile.dev", "json gateway preview profile id mismatch");
        require(json["ready"], "json gateway preview ready mismatch");
        require(json["tools"].size() == 1, "json gateway preview tools count mismatch");
        require(json["tools"][0]["name"] == "filesystem.read_file", "json gateway preview tool name mismatch");
        require(json["prompts"].empty(), "json gateway preview prompts should be empty");
        require(json["resources"].empty(), "json gateway preview resources should be empty");
        require(json["issues"].empty(), "json gateway preview issues should be empty");
    }

    void test_serves_gateway_tools_list_over_stdio() {
        CliHarness harness;
        require(harness.servers_.save(mcp_server()).has_value(), "test cxxmcp server save failed");
        const auto saved = harness.capabilities_.replace_for_server(
                "filesystem",
                {
                        capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                   "filesystem.read_file"),
                });
        require(saved.has_value(), "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        std::istringstream input(
                R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})"
                "\n");
        const auto exit_code = harness.run_with_input({"gateway", "serve-stdio", "profile.dev"}, input);

        require(exit_code == 0, "gateway serve-stdio exit code mismatch");
        require(harness.err.str().empty(), "gateway serve-stdio should not write stderr");
        require(harness.out.str().find("\"name\":\"filesystem.read_file\"") != std::string::npos,
                "gateway serve-stdio tools/list output mismatch");
    }

    void test_gateway_serve_stdio_rejects_unready_profile_bindings() {
        CliHarness harness;
        auto server = mcp_server();
        server.trust = mcp::app::McpServerTrustState::untrusted;
        require(harness.servers_.save(server).has_value(), "test untrusted cxxmcp server save failed");
        require(harness.capabilities_.replace_for_server(
                                             "filesystem",
                                             {capability("filesystem:tool:read_file", mcp::app::CapabilityKind::tool, "read_file",
                                                         "filesystem.read_file")})
                        .has_value(),
                "test capability save failed");
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");
        require(harness.run({"exposures", "bind", "profile.dev", "filesystem:tool:read_file"}) == 0,
                "exposure bind failed");

        harness.out.str({});
        harness.out.clear();
        std::istringstream input(R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})"
                                 "\n");
        const auto exit_code = harness.run_with_input({"gateway", "serve-stdio", "profile.dev"}, input);

        require(exit_code == 1, "gateway serve-stdio should reject unready bindings");
        require(harness.out.str().empty(), "gateway serve-stdio unready profile should not write stdout");
        require(harness.err.str().find("gateway profile is not ready: profile.dev") != std::string::npos,
                "gateway serve-stdio readiness header mismatch");
        require(harness.err.str().find("cxxmcp server is untrusted: filesystem") != std::string::npos,
                "gateway serve-stdio readiness issue mismatch");
        require(harness.err.str().find("hint: trust the server with: cxxmcp servers trust filesystem") !=
                        std::string::npos,
                "gateway serve-stdio readiness hint mismatch");
    }

    void test_gateway_serve_stdio_rejects_empty_profile_bindings() {
        CliHarness harness;
        require(harness.run({"exposures", "create", "profile.empty", "Empty"}) == 0,
                "empty exposure create failed");

        harness.out.str({});
        harness.out.clear();
        harness.err.str({});
        harness.err.clear();
        std::istringstream input(R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})"
                                 "\n");
        const auto exit_code = harness.run_with_input({"gateway", "serve-stdio", "profile.empty"}, input);

        require(exit_code == 1, "gateway serve-stdio should reject empty bindings");
        require(harness.out.str().empty(), "gateway serve-stdio empty profile should not write stdout");
        require(harness.err.str().find("gateway profile is not ready: profile.empty") != std::string::npos,
                "gateway serve-stdio empty readiness header mismatch");
        require(harness.err.str().find("no enabled capability bindings configured: profile.empty") !=
                        std::string::npos,
                "gateway serve-stdio empty readiness issue mismatch");
    }

    void test_gateway_serve_http_requires_configured_endpoint() {
        CliHarness harness;
        require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");

        harness.out.str({});
        harness.out.clear();
        const auto exit_code = harness.run({"gateway", "serve-http", "profile.dev"});

        require(exit_code == 1, "gateway serve-http should fail without endpoint port");
        require(harness.out.str().empty(), "gateway serve-http should not write stdout on endpoint error");
        require(harness.err.str().find("exposure endpoint port is not configured") != std::string::npos,
                "gateway serve-http endpoint error mismatch");
    }

    void test_gateway_serve_all_requires_profiles_and_endpoints() {
        {
            CliHarness harness;
            const auto exit_code = harness.run({"gateway", "serve-all"});

            require(exit_code == 1, "gateway serve-all should fail without configured profiles");
            require(harness.out.str().empty(), "gateway serve-all should not write stdout without profiles");
            require(harness.err.str() == "no gateway profiles configured\n",
                    "gateway serve-all empty runtime error mismatch");
        }

        {
            CliHarness harness;
            require(harness.run({"exposures", "create", "profile.dev", "Dev"}) == 0, "exposure create failed");

            harness.out.str({});
            harness.out.clear();
            harness.err.str({});
            harness.err.clear();
            const auto exit_code = harness.run({"gateway", "serve-all"});

            require(exit_code == 1, "gateway serve-all should fail when a profile has no HTTP endpoint");
            require(harness.out.str().empty(), "gateway serve-all should not write stdout on endpoint error");
            require(harness.err.str() == "exposure endpoint port is not configured: profile.dev\n",
                    "gateway serve-all endpoint error mismatch");
        }

        {
            CliHarness harness;
            require(harness.run({"exposures", "create", "profile.empty", "Empty"}) == 0,
                    "ready-only empty exposure create failed");
            require(harness.run({"exposures", "endpoint", "profile.empty", "127.0.0.1", "39919", "cxxmcp/empty"}) == 0,
                    "ready-only empty exposure endpoint failed");

            harness.out.str({});
            harness.out.clear();
            harness.err.str({});
            harness.err.clear();
            const auto exit_code = harness.run({"gateway", "serve-all", "--ready-only"});

            require(exit_code == 1, "gateway serve-all ready-only should fail without ready profiles");
            require(harness.out.str().empty(), "gateway serve-all ready-only should not write stdout without ready profiles");
            require(harness.err.str().find("Skipping gateway profile profile.empty\n") != std::string::npos,
                    "gateway serve-all ready-only skip output mismatch");
            require(harness.err.str().find("no ready gateway profiles configured\n") != std::string::npos,
                    "gateway serve-all ready-only no ready error mismatch");
        }

        {
            CliHarness harness;
            require(harness.run({"exposures", "create", "profile.empty", "Empty"}) == 0,
                    "empty exposure create failed");
            require(harness.run({"exposures", "endpoint", "profile.empty", "127.0.0.1", "39919", "cxxmcp/empty"}) == 0,
                    "empty exposure endpoint failed");

            harness.out.str({});
            harness.out.clear();
            harness.err.str({});
            harness.err.clear();
            const auto exit_code = harness.run({"gateway", "serve-all"});

            require(exit_code == 1, "gateway serve-all should reject empty bindings");
            require(harness.out.str().empty(), "gateway serve-all empty profile should not write stdout");
            require(harness.err.str().find("gateway profile is not ready: profile.empty") != std::string::npos,
                    "gateway serve-all empty readiness header mismatch");
            require(harness.err.str().find("no enabled capability bindings configured: profile.empty") !=
                            std::string::npos,
                    "gateway serve-all empty readiness issue mismatch");
        }
    }

    void test_gateway_serve_http_end_to_end() {
        const auto state_dir = std::filesystem::temp_directory_path() / "mcp-cli-http-test";
        const auto port = reserve_free_port();
        std::error_code ec;
        std::filesystem::remove_all(state_dir, ec);
        std::filesystem::create_directories(state_dir, ec);
        require(!ec, "state directory setup failed");

        const auto cli = std::filesystem::path(MCP_TEST_CLI_EXE);
        const auto state_dir_arg = quote_wide(state_dir);

#ifdef _WIN32
        const auto child = std::filesystem::path(MCP_TEST_CHILD_EXE);
        const auto init_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                state_dir_arg,
                L"gateway",
                L"init-stdio",
                L"--trust",
                L"--discover",
                L"--path",
                L"/cxxmcp/cli",
                L"profile.dev",
                L"child",
                L"127.0.0.1",
                std::to_wstring(port),
                quote_wide(child),
        }));
        require(init_result.has_value(), "gateway serve-http setup init-stdio should complete");
        require(init_result->first == 0, "gateway serve-http setup init-stdio should succeed");

        const auto serve_command = make_command_line({
                quote_wide(cli),
                L"--state-dir",
                state_dir_arg,
                L"gateway",
                L"serve-http",
                L"profile.dev",
        });
        auto served = start_process(serve_command);
        require(served.has_value(), "gateway serve-http process should start");
        struct ProcessCleanup {
            PROCESS_INFORMATION &process;
            ~ProcessCleanup() {
                if (process.hProcess != nullptr) {
                    TerminateProcess(process.hProcess, 0);
                    WaitForSingleObject(process.hProcess, 5000);
                    close_process(process);
                }
            }
        } cleanup{*served};
        require(wait_for_http_gateway(port, "/cxxmcp/cli"), "gateway serve-http should become reachable");

        mcp::client::Client client(std::make_unique<mcp::client::HttpTransport>(
                mcp::client::HttpTransportOptions{
                        .host = "127.0.0.1",
                        .port = port,
                        .path = "/cxxmcp/cli",
                        .headers = {},
                        .timeout = std::chrono::milliseconds{1000},
                }));
        const auto ping = client.raw_request(mcp::protocol::make_ping_request(std::int64_t{1}));
        require(ping.has_value(), "gateway serve-http ping should succeed");
        require(ping->empty(), "gateway serve-http ping response should be empty");
#else
        (void) cli;
        (void) state_dir_arg;
        require(false, "gateway serve-http process test is only implemented on Windows");
#endif

        std::filesystem::remove_all(state_dir, ec);
    }

    void test_cli_binary_gateway_serve_http_proxies_stdio_child() {
#ifdef _WIN32
        const auto cli = std::filesystem::path(MCP_TEST_CLI_EXE);
        const auto child = std::filesystem::path(MCP_TEST_CHILD_EXE);
        const auto port = reserve_free_port();
        const auto state_dir = std::filesystem::temp_directory_path() /
                               ("mcp-cli-http-proxy-" + std::to_string(GetCurrentProcessId()) + "-" +
                                std::to_string(GetTickCount64()));
        std::error_code ec;
        std::filesystem::remove_all(state_dir, ec);
        std::filesystem::create_directories(state_dir, ec);
        require(!ec, "binary http proxy setup should succeed");

        const auto add_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"servers",
                L"add-stdio",
                L"child",
                quote_wide(child),
        }));
        require(add_result.has_value(), "binary http proxy add-stdio process should succeed");
        require(add_result->first == 0, "binary http proxy add-stdio exit code mismatch");

        const auto init_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"gateway",
                L"init",
                L"--trust",
                L"--discover",
                L"profile.http",
                L"child",
                L"127.0.0.1",
                std::to_wstring(port),
                L"/cxxmcp/http",
        }));
        require(init_result.has_value(), "binary http proxy gateway init process should succeed");
        require(init_result->first == 0, "binary http proxy gateway init exit code mismatch");

        const auto instructions_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"exposures",
                L"set-instructions",
                L"profile.http",
                L"\"Use the HTTP child gateway.\"",
        }));
        require(instructions_result.has_value(), "binary http proxy set-instructions process should succeed");
        require(instructions_result->first == 0, "binary http proxy set-instructions exit code mismatch");

        const auto serve_command = make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"gateway",
                L"serve-http",
                L"profile.http",
        });
        auto served = start_process(serve_command);
        require(served.has_value(), "binary http proxy gateway serve-http process should start");
        struct ProcessCleanup {
            PROCESS_INFORMATION &process;
            ~ProcessCleanup() {
                if (process.hProcess != nullptr) {
                    TerminateProcess(process.hProcess, 0);
                    WaitForSingleObject(process.hProcess, 5000);
                    close_process(process);
                }
            }
        } cleanup{*served};
        require(wait_for_http_gateway(port, "/cxxmcp/http"), "binary http proxy gateway should become reachable");

        mcp::client::Client client(std::make_unique<mcp::client::HttpTransport>(
                mcp::client::HttpTransportOptions{
                        .host = "127.0.0.1",
                        .port = port,
                        .path = "/cxxmcp/http",
                        .headers = {},
                        .timeout = std::chrono::milliseconds{1000},
                }));

        const auto initialized = client.raw_request(mcp::protocol::make_initialize_request(std::int64_t{1}));
        require(initialized.has_value(), "binary http proxy initialize should succeed");
        require(initialized->at("instructions") == "Use the HTTP child gateway.",
                "binary http proxy initialize instructions mismatch");

        const auto tools = client.list_tools();
        require(tools.has_value(), "binary http proxy tools/list should succeed");
        require(tools->size() == 1 && tools->front().name == "child.echo",
                "binary http proxy tools/list output mismatch");

        const auto called = client.call_tool(mcp::protocol::ToolCall{
                .name = "child.echo",
                .arguments = Json{{"message", "hello"}},
        });
        require(called.has_value(), "binary http proxy tools/call should succeed");
        require(!called->content.empty() && called->content.front().text == "echo",
                "binary http proxy tools/call content mismatch");
        require(called->structured_content.has_value() && called->structured_content->value("message", "") == "hello",
                "binary http proxy tools/call structured content mismatch");

        const auto prompt = client.get_prompt(mcp::protocol::PromptsGetParams{
                .name = "child.summarize",
                .arguments = Json{{"text", "hello"}},
        });
        require(prompt.has_value(), "binary http proxy prompts/get should succeed");
        require(!prompt->messages.empty() && prompt->messages.front().content.text == "Summarize hello",
                "binary http proxy prompts/get output mismatch");

        const auto resource = client.read_resource(mcp::protocol::ResourcesReadParams{
                .uri = "file:///workspace/README.md",
        });
        require(resource.has_value(), "binary http proxy resources/read should succeed");
        require(!resource->contents.empty() && resource->contents.front().text == "hello from readme",
                "binary http proxy resources/read output mismatch");

        std::filesystem::remove_all(state_dir, ec);
#else
        require(false, "binary HTTP gateway proxy test is only implemented on Windows");
#endif
    }

    void test_cli_binary_gateway_serve_http_isolates_profiles() {
#ifdef _WIN32
        const auto cli = std::filesystem::path(MCP_TEST_CLI_EXE);
        const auto child = std::filesystem::path(MCP_TEST_CHILD_EXE);
        const auto alpha_port = reserve_free_port();
        auto beta_port = reserve_free_port();
        if (beta_port == alpha_port) {
            beta_port = reserve_free_port();
        }
        require(beta_port != alpha_port, "binary http multi-profile ports should differ");

        const auto state_dir = std::filesystem::temp_directory_path() /
                               ("mcp-cli-http-profiles-" + std::to_string(GetCurrentProcessId()) + "-" +
                                std::to_string(GetTickCount64()));
        std::error_code ec;
        std::filesystem::remove_all(state_dir, ec);
        std::filesystem::create_directories(state_dir, ec);
        require(!ec, "binary http multi-profile setup should succeed");

        const auto add_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"servers",
                L"add-stdio",
                L"child",
                quote_wide(child),
        }));
        require(add_result.has_value(), "binary http multi-profile add-stdio process should succeed");
        require(add_result->first == 0, "binary http multi-profile add-stdio exit code mismatch");

        const auto init_alpha = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"gateway",
                L"init",
                L"--trust",
                L"--discover",
                L"--instructions",
                L"\"Use alpha profile tools.\"",
                L"profile.alpha",
                L"child",
                L"127.0.0.1",
                std::to_wstring(alpha_port),
                L"/cxxmcp/alpha",
        }));
        require(init_alpha.has_value(), "binary http multi-profile alpha init process should succeed");
        require(init_alpha->first == 0, "binary http multi-profile alpha init exit code mismatch");

        const auto init_beta = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"gateway",
                L"init",
                L"--trust",
                L"--discover",
                L"--instructions",
                L"\"Use beta profile tools.\"",
                L"profile.beta",
                L"child",
                L"127.0.0.1",
                std::to_wstring(beta_port),
                L"/cxxmcp/beta",
        }));
        require(init_beta.has_value(), "binary http multi-profile beta init process should succeed");
        require(init_beta->first == 0, "binary http multi-profile beta init exit code mismatch");

        const auto serve_all_command = make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"gateway",
                L"serve-all",
        });
        auto served_all = start_process(serve_all_command);
        require(served_all.has_value(), "binary http multi-profile serve-all process should start");
        struct ProcessCleanup {
            PROCESS_INFORMATION &process;
            ~ProcessCleanup() {
                if (process.hProcess != nullptr) {
                    TerminateProcess(process.hProcess, 0);
                    WaitForSingleObject(process.hProcess, 5000);
                    close_process(process);
                }
            }
        };
        ProcessCleanup cleanup_all{*served_all};

        require(wait_for_http_gateway(alpha_port, "/cxxmcp/alpha"),
                "binary http multi-profile alpha gateway should become reachable");
        require(wait_for_http_gateway(beta_port, "/cxxmcp/beta"),
                "binary http multi-profile beta gateway should become reachable");

        mcp::client::Client alpha_client(std::make_unique<mcp::client::HttpTransport>(
                mcp::client::HttpTransportOptions{
                        .host = "127.0.0.1",
                        .port = alpha_port,
                        .path = "/cxxmcp/alpha",
                        .headers = {},
                        .timeout = std::chrono::milliseconds{1000},
                }));
        mcp::client::Client beta_client(std::make_unique<mcp::client::HttpTransport>(
                mcp::client::HttpTransportOptions{
                        .host = "127.0.0.1",
                        .port = beta_port,
                        .path = "/cxxmcp/beta",
                        .headers = {},
                        .timeout = std::chrono::milliseconds{1000},
                }));

        const auto alpha_initialized =
                alpha_client.raw_request(mcp::protocol::make_initialize_request(std::int64_t{1}));
        require(alpha_initialized.has_value(), "binary http multi-profile alpha initialize should succeed");
        require(alpha_initialized->at("instructions") == "Use alpha profile tools.",
                "binary http multi-profile alpha instructions mismatch");

        const auto beta_initialized =
                beta_client.raw_request(mcp::protocol::make_initialize_request(std::int64_t{1}));
        require(beta_initialized.has_value(), "binary http multi-profile beta initialize should succeed");
        require(beta_initialized->at("instructions") == "Use beta profile tools.",
                "binary http multi-profile beta instructions mismatch");

        const auto alpha_tools = alpha_client.list_tools();
        require(alpha_tools.has_value(), "binary http multi-profile alpha tools/list should succeed");
        require(alpha_tools->size() == 1 && alpha_tools->front().name == "child.echo",
                "binary http multi-profile alpha tools/list output mismatch");

        const auto beta_tools = beta_client.list_tools();
        require(beta_tools.has_value(), "binary http multi-profile beta tools/list should succeed");
        require(beta_tools->size() == 1 && beta_tools->front().name == "child.echo",
                "binary http multi-profile beta tools/list output mismatch");

        std::filesystem::remove_all(state_dir, ec);
#else
        require(false, "binary HTTP gateway multi-profile test is only implemented on Windows");
#endif
    }

    void test_invalid_command_returns_usage_error() {
        CliHarness harness;

        const auto exit_code = harness.run({"tools"});

        require(exit_code == 2, "invalid command exit code mismatch");
        require(harness.out.str().empty(), "invalid command should not write stdout");
        require(harness.err.str().find("invalid tools command") != std::string::npos, "missing parse error");
        require(harness.err.str().find("cxxmcp tools list") != std::string::npos, "missing usage");
    }

    void test_command_group_help_outputs_scoped_usage() {
        {
            CliHarness harness;
            const auto exit_code = harness.run({"gateway", "help"});

            require(exit_code == 0, "gateway help exit code mismatch");
            require(harness.err.str().empty(), "gateway help should not write stderr");
            require(harness.out.str().find("cxxmcp gateway init [--trust] [--discover]") != std::string::npos,
                    "gateway help missing init command");
            require(harness.out.str().find("cxxmcp gateway client-config-stdio <profile-id> [server-name]") !=
                            std::string::npos,
                    "gateway help missing stdio client config command");
            require(harness.out.str().find("cxxmcp gateway client-config-all [--ready-only] [server-name-prefix]") !=
                            std::string::npos,
                    "gateway help missing all client config command");
            require(harness.out.str().find("cxxmcp gateway list") != std::string::npos,
                    "gateway help missing list command");
            require(harness.out.str().find("cxxmcp gateway inspect <profile-id>") != std::string::npos,
                    "gateway help missing inspect command");
            require(harness.out.str().find("cxxmcp gateway status") != std::string::npos,
                    "gateway help missing status command");
            require(harness.out.str().find("cxxmcp gateway check-all") != std::string::npos,
                    "gateway help missing check-all command");
            require(harness.out.str().find("cxxmcp gateway serve-all") != std::string::npos,
                    "gateway help missing serve-all command");
            require(harness.out.str().find("cxxmcp gateway init-stdio [--trust] [--discover]") !=
                            std::string::npos,
                    "gateway help missing init-stdio command");
            require(harness.out.str().find("cxxmcp gateway init-http [--trust] [--discover]") !=
                            std::string::npos,
                    "gateway help missing init-http command");
            require(harness.out.str().find(
                            "cxxmcp gateway init-all [--trust] [--discover] [--profile-prefix <prefix>] [--instructions <text>] <host> <base-port> [path-prefix]") !=
                            std::string::npos,
                    "gateway help missing init-all command");
            require(harness.out.str().find(
                            "cxxmcp gateway import-config [--trust] [--discover] [--profile-prefix <prefix>] [--instructions <text>] <config-path> <host> <base-port> [path-prefix]") !=
                            std::string::npos,
                    "gateway help missing import-config command");
            require(harness.out.str().find("[--instructions <text>]") != std::string::npos,
                    "gateway help missing instructions option");
            require(harness.out.str().find("Fast path:") != std::string::npos,
                    "gateway help missing fast path section");
            require(harness.out.str().find("Use client-config-stdio when the downstream client should start this gateway process itself.") !=
                            std::string::npos,
                    "gateway help missing stdio client config guidance");
            require(harness.out.str().find("cxxmcp servers add-stdio") == std::string::npos,
                    "gateway help should be scoped to gateway commands");
        }

        {
            CliHarness harness;
            const auto exit_code = harness.run({"servers", "--help"});

            require(exit_code == 0, "servers help exit code mismatch");
            require(harness.err.str().empty(), "servers help should not write stderr");
            require(harness.out.str().find("cxxmcp servers add-stdio [--trust] [--discover] [--cwd <cwd>] [--env <name> <value>]... <server-id> <command> [args...]") !=
                            std::string::npos,
                    "servers help missing add-stdio command");
            require(harness.out.str().find("cxxmcp servers import [--trust] [--discover] <path>") !=
                            std::string::npos,
                    "servers help missing import options");
            require(harness.out.str().find("cxxmcp servers add-http [--trust] [--discover] [--header <name> <value>]... <server-id> <url>") !=
                            std::string::npos,
                    "servers help missing add-http header command");
            require(harness.out.str().find("cxxmcp servers set-header <server-id> <name> <value>") !=
                            std::string::npos,
                    "servers help missing set-header command");
            require(harness.out.str().find("Servers must be trusted before discovery or gateway routing.") !=
                            std::string::npos,
                    "servers help missing trust guidance");
            require(harness.out.str().find("cxxmcp servers add-http --trust --discover --header Authorization \"Bearer token\" remote http://127.0.0.1:3000/mcp") !=
                            std::string::npos,
                    "servers help missing add-http header example");
            require(harness.out.str().find("cxxmcp servers add-stdio --trust --discover --cwd C:\\workspace --env API_TOKEN secret filesystem node server.js --root C:\\workspace") !=
                            std::string::npos,
                    "servers help missing add-stdio launch option example");
            require(harness.out.str().find("cxxmcp gateway serve-http") == std::string::npos,
                    "servers help should be scoped to server commands");
        }

        {
            CliHarness harness;
            const auto exit_code = harness.run({"exposures", "-h"});

            require(exit_code == 0, "exposures help exit code mismatch");
            require(harness.err.str().empty(), "exposures help should not write stderr");
            require(harness.out.str().find("cxxmcp exposures bind-server <profile-id> <server-id>") !=
                            std::string::npos,
                    "exposures help missing bind-server command");
            require(harness.out.str().find("cxxmcp exposures prune <profile-id>") != std::string::npos,
                    "exposures help missing prune command");
            require(harness.out.str().find("A profile chooses endpoint, instructions, and exposed upstream capabilities.") !=
                            std::string::npos,
                    "exposures help missing profile guidance");
            require(harness.out.str().find("cxxmcp exposures bind-server profile.dev filesystem") !=
                            std::string::npos,
                    "exposures help missing bind-server example");
            require(harness.out.str().find("cxxmcp servers discover-all") == std::string::npos,
                    "exposures help should be scoped to exposure commands");
        }
    }

    void test_runtime_options_parse_state_dir_and_version() {
        std::vector<std::string_view> args{
                "--state-dir",
                "C:/tmp/mcp-state",
                "--version",
                "tools",
                "list",
        };

        const auto parsed = mcp::cli::parse_runtime_options(args);
        require(parsed.has_value(), "runtime options parse should succeed");
        require(parsed->show_version, "version flag should be set");
        require(parsed->state_directory == std::filesystem::path("C:/tmp/mcp-state"),
                "state directory mismatch");
        require(args.size() == 2 && args[0] == "tools" && args[1] == "list",
                "runtime options should strip global flags");
    }

    void test_runtime_options_parse_equals_form_and_short_version() {
        std::vector<std::string_view> args{
                "-V",
                "--state-dir=C:/tmp/mcp-state",
                "tools",
                "list",
        };

        const auto parsed = mcp::cli::parse_runtime_options(args);
        require(parsed.has_value(), "runtime options parse should succeed");
        require(parsed->show_version, "short version flag should be set");
        require(parsed->state_directory == std::filesystem::path("C:/tmp/mcp-state"),
                "equals-form state directory mismatch");
        require(args.size() == 2 && args[0] == "tools" && args[1] == "list",
                "equals-form runtime options should strip global flags");
    }

    void test_runtime_options_parse_json_output() {
        std::vector<std::string_view> args{
                "--json",
                "servers",
                "list",
        };

        const auto parsed = mcp::cli::parse_runtime_options(args);
        require(parsed.has_value(), "runtime options parse should succeed");
        require(parsed->json_output, "json output flag should be set");
        require(args.size() == 2 && args[0] == "servers" && args[1] == "list",
                "json runtime options should strip global flag");
    }

    void test_runtime_options_parse_global_help() {
        std::vector<std::string_view> args{"--help", "tools", "list"};

        const auto parsed = mcp::cli::parse_runtime_options(args);
        require(parsed.has_value(), "runtime options parse should succeed");
        require(parsed->show_help, "help flag should be set");
        require(args.size() == 2 && args[0] == "tools" && args[1] == "list",
                "help runtime options should strip global flags");
    }

    void test_runtime_options_preserves_command_help() {
        std::vector<std::string_view> args{"gateway", "--help"};

        const auto parsed = mcp::cli::parse_runtime_options(args);
        require(parsed.has_value(), "runtime options parse should succeed");
        require(!parsed->show_help, "command help should not become global help");
        require(args.size() == 2 && args[0] == "gateway" && args[1] == "--help",
                "command help should remain in command args");
    }

    void test_runtime_options_rejects_missing_state_dir_value() {
        std::vector<std::string_view> args{"--state-dir"};

        const auto parsed = mcp::cli::parse_runtime_options(args);
        require(!parsed.has_value(), "missing state dir should fail");
        require(parsed.error().message == "missing state directory", "missing state dir error mismatch");
    }

    void test_cli_binary_help_and_version_output() {
#ifdef _WIN32
        const auto cli = std::filesystem::path(MCP_TEST_CLI_EXE);

        {
            const auto captured = run_process_capture(make_command_line({quote_wide(cli), L"--help"}));
            require(captured.has_value(), "help process should succeed");
            require(captured->first == 0, "help exit code mismatch");
            require(captured->second.find("cxxmcp [--help] [--json] [--state-dir <path>] [--version] <command>") !=
                            std::string::npos,
                    "help output mismatch");
            require(captured->second.find("cxxmcp exposures prune <profile-id>") != std::string::npos,
                    "help output should include exposure prune command");
            require(captured->second.find("cxxmcp gateway init [--trust] [--discover]") != std::string::npos,
                    "help output should include gateway init command");
            require(captured->second.find("cxxmcp gateway client-config-stdio <profile-id> [server-name]") !=
                            std::string::npos,
                    "help output should include gateway stdio client config command");
            require(captured->second.find("cxxmcp gateway client-config-all [--ready-only] [server-name-prefix]") !=
                            std::string::npos,
                    "help output should include gateway all client config command");
            require(captured->second.find("cxxmcp gateway inspect <profile-id>") != std::string::npos,
                    "help output should include gateway inspect command");
            require(captured->second.find("cxxmcp gateway check-all") != std::string::npos,
                    "help output should include gateway check-all command");
            require(captured->second.find("cxxmcp gateway init-stdio [--trust] [--discover]") !=
                            std::string::npos,
                    "help output should include gateway init-stdio command");
            require(captured->second.find("cxxmcp gateway init-http [--trust] [--discover]") !=
                            std::string::npos,
                    "help output should include gateway init-http command");
            require(captured->second.find(
                            "cxxmcp gateway init-all [--trust] [--discover] [--profile-prefix <prefix>] [--instructions <text>] <host> <base-port> [path-prefix]") !=
                            std::string::npos,
                    "help output should include gateway init-all command");
            require(captured->second.find(
                            "cxxmcp gateway import-config [--trust] [--discover] [--profile-prefix <prefix>] [--instructions <text>] <config-path> <host> <base-port> [path-prefix]") !=
                            std::string::npos,
                    "help output should include gateway import-config command");
            require(captured->second.find("Common workflows:") != std::string::npos,
                    "help output should include workflow examples");
            require(captured->second.find("Use scoped help for details: cxxmcp gateway help, cxxmcp servers help, cxxmcp exposures help.") !=
                            std::string::npos,
                    "help output should point to scoped help");
        }

        {
            const auto captured = run_process_capture(make_command_line({quote_wide(cli), L"gateway", L"--help"}));
            require(captured.has_value(), "gateway help process should succeed");
            require(captured->first == 0, "gateway help exit code mismatch");
            require(captured->second.find("cxxmcp gateway init [--trust] [--discover]") != std::string::npos,
                    "binary gateway help output mismatch");
            require(captured->second.find("cxxmcp gateway list") != std::string::npos,
                    "binary gateway help should include list command");
            require(captured->second.find("cxxmcp gateway inspect <profile-id>") != std::string::npos,
                    "binary gateway help should include inspect command");
            require(captured->second.find("cxxmcp gateway check-all") != std::string::npos,
                    "binary gateway help should include check-all command");
            require(captured->second.find("cxxmcp gateway status") != std::string::npos,
                    "binary gateway help should include status command");
            require(captured->second.find("cxxmcp gateway client-config-all [--ready-only] [server-name-prefix]") !=
                            std::string::npos,
                    "binary gateway help should include all client config command");
            require(captured->second.find("cxxmcp gateway init-stdio [--trust] [--discover]") !=
                            std::string::npos,
                    "binary gateway help should include init-stdio command");
            require(captured->second.find("cxxmcp gateway init-http [--trust] [--discover]") !=
                            std::string::npos,
                    "binary gateway help should include init-http command");
            require(captured->second.find(
                            "cxxmcp gateway init-all [--trust] [--discover] [--profile-prefix <prefix>] [--instructions <text>] <host> <base-port> [path-prefix]") !=
                            std::string::npos,
                    "binary gateway help should include init-all command");
            require(captured->second.find(
                            "cxxmcp gateway import-config [--trust] [--discover] [--profile-prefix <prefix>] [--instructions <text>] <config-path> <host> <base-port> [path-prefix]") !=
                            std::string::npos,
                    "binary gateway help should include import-config command");
            require(captured->second.find("cxxmcp servers add-stdio") == std::string::npos,
                    "binary gateway help should be scoped");
        }

        {
            const auto captured = run_process_capture(make_command_line({quote_wide(cli), L"-V"}));
            require(captured.has_value(), "version process should succeed");
            require(captured->first == 0, "version exit code mismatch");
            require(captured->second == "cxxmcp 2.0.0\n", "version output mismatch");
        }
#else
        require(false, "binary cli output test is only implemented on Windows");
#endif
    }

    void test_cli_binary_state_dir_override() {
#ifdef _WIN32
        const auto cli = std::filesystem::path(MCP_TEST_CLI_EXE);
        const auto state_dir = std::filesystem::temp_directory_path() / "mcp-cli-binary-state";
        std::error_code ec;
        std::filesystem::remove_all(state_dir, ec);
        std::filesystem::create_directories(state_dir, ec);
        require(!ec, "binary state directory setup should succeed");

        const auto captured = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"exposures",
                L"create",
                L"profile.dev",
                L"Dev",
        }));
        require(captured.has_value(), "state dir process should succeed");
        require(captured->first == 0, "state dir exit code mismatch");
        require(captured->second == "Created exposure profile profile.dev\n", "state dir output mismatch");
        require(std::filesystem::exists(state_dir / "exposure_profiles.json"),
                "state dir should contain exposure profiles file");

        const auto endpoint_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"exposures",
                L"endpoint",
                L"profile.dev",
                L"127.0.0.1",
                L"39919",
                L"cxxmcp/dev",
        }));
        require(endpoint_result.has_value(), "binary exposure endpoint process should succeed");
        require(endpoint_result->first == 0, "binary exposure endpoint exit code mismatch");

        const auto client_config_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"gateway",
                L"client-config",
                L"profile.dev",
                L"dev-gateway",
        }));
        require(client_config_result.has_value(), "binary gateway client-config process should succeed");
        require(client_config_result->first == 1, "binary gateway client-config should fail without bindings");
        require(client_config_result->second.find("gateway profile is not ready: profile.dev") != std::string::npos,
                "binary gateway client-config readiness error mismatch");
        require(client_config_result->second.find("no enabled capability bindings configured") != std::string::npos,
                "binary gateway client-config readiness issue mismatch");

        const auto stdio_client_config_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"gateway",
                L"client-config-stdio",
                L"profile.dev",
                L"dev-gateway",
        }));
        require(stdio_client_config_result.has_value(), "binary gateway client-config-stdio process should succeed");
        require(stdio_client_config_result->first == 1,
                "binary gateway client-config-stdio should fail without bindings");
        require(stdio_client_config_result->second.find("gateway profile is not ready: profile.dev") !=
                        std::string::npos,
                "binary gateway client-config-stdio readiness error mismatch");
        require(stdio_client_config_result->second.find("no enabled capability bindings configured") !=
                        std::string::npos,
                "binary gateway client-config-stdio readiness issue mismatch");

        const auto check_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"gateway",
                L"check",
                L"profile.dev",
        }));
        require(check_result.has_value(), "binary gateway check process should complete");
        require(check_result->first == 1, "binary gateway check should fail without bindings");
        require(check_result->second.find("no enabled capability bindings configured") != std::string::npos,
                "binary gateway check output mismatch");

        std::filesystem::remove_all(state_dir, ec);
#else
        require(false, "binary state dir test is only implemented on Windows");
#endif
    }

    void test_cli_binary_creates_missing_state_dir() {
#ifdef _WIN32
        const auto cli = std::filesystem::path(MCP_TEST_CLI_EXE);
        const auto state_dir = std::filesystem::temp_directory_path() /
                               ("mcp-cli-missing-state-" + std::to_string(GetCurrentProcessId()) + "-" +
                                std::to_string(GetTickCount64()));
        std::error_code ec;
        std::filesystem::remove_all(state_dir, ec);
        require(!std::filesystem::exists(state_dir), "missing state dir test should start without directory");

        const auto captured = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"exposures",
                L"create",
                L"profile.first",
                L"First",
        }));

        require(captured.has_value(), "missing state dir process should succeed");
        require(captured->first == 0, "missing state dir exit code mismatch");
        require(captured->second == "Created exposure profile profile.first\n", "missing state dir output mismatch");
        require(std::filesystem::exists(state_dir), "missing state dir should be created");
        require(std::filesystem::exists(state_dir / "exposure_profiles.json"),
                "missing state dir should contain exposure profiles file");
        std::filesystem::remove_all(state_dir, ec);
#else
        require(false, "binary missing state dir test is only implemented on Windows");
#endif
    }

    void test_cli_binary_uses_runtime_home_environment() {
#ifdef _WIN32
        const auto cli = std::filesystem::path(MCP_TEST_CLI_EXE);
        const auto runtime_home = std::filesystem::temp_directory_path() /
                                  ("mcp-cli-runtime-home-" + std::to_string(GetCurrentProcessId()) + "-" +
                                   std::to_string(GetTickCount64()));
        std::error_code ec;
        std::filesystem::remove_all(runtime_home, ec);
        std::filesystem::create_directories(runtime_home, ec);
        require(!ec, "runtime home setup should succeed");

        char *previous_home = nullptr;
        std::size_t previous_home_size = 0;
        const bool had_previous_home =
                _dupenv_s(&previous_home, &previous_home_size, "MCP_RUNTIME_HOME") == 0 && previous_home != nullptr;
        const std::string previous_home_value = had_previous_home ? previous_home : std::string{};
        std::free(previous_home);

        const auto runtime_home_value = runtime_home.string();
        require(_putenv_s("MCP_RUNTIME_HOME", runtime_home_value.c_str()) == 0,
                "runtime home environment override should succeed");

        const auto captured = run_process_capture(make_command_line({
                quote_wide(cli),
                L"exposures",
                L"create",
                L"profile.env",
                L"Env",
        }));
        if (!had_previous_home) {
            _putenv_s("MCP_RUNTIME_HOME", "");
        } else {
            _putenv_s("MCP_RUNTIME_HOME", previous_home_value.c_str());
        }

        require(captured.has_value(), "runtime home process should succeed");
        require(captured->first == 0, "runtime home exit code mismatch");
        require(captured->second == "Created exposure profile profile.env\n", "runtime home output mismatch");
        require(std::filesystem::exists(runtime_home / "exposure_profiles.json"),
                "runtime home should contain exposure profiles file");
        std::filesystem::remove_all(runtime_home, ec);
#else
        require(false, "binary runtime home test is only implemented on Windows");
#endif
    }

    void test_cli_binary_servers_import_and_list() {
#ifdef _WIN32
        const auto cli = std::filesystem::path(MCP_TEST_CLI_EXE);
        const auto state_dir = std::filesystem::temp_directory_path() /
                               ("mcp-cli-binary-servers-" + std::to_string(GetCurrentProcessId()) + "-" +
                                std::to_string(GetTickCount64()));
        const auto config_path = state_dir / "client-config.json";
        std::error_code ec;
        std::filesystem::remove_all(state_dir, ec);
        std::filesystem::create_directories(state_dir, ec);
        require(!ec, "binary servers setup should succeed");

        {
            std::ofstream output(config_path);
            output << R"({
  "mcpServers": {
    "filesystem": {
      "command": "node",
      "args": ["server.js"]
    }
  }
})";
        }

        const auto import_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"servers",
                L"import",
                quote_wide(config_path),
        }));
        require(import_result.has_value(), "servers import process should succeed");
        require(import_result->first == 0, "servers import exit code mismatch");
        require(import_result->second == "Imported 1 cxxmcp server(s)\n", "servers import output mismatch");
        require(std::filesystem::exists(state_dir / "servers.json"), "servers state file should exist");

        const auto set_cwd_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"servers",
                L"set-cwd",
                L"filesystem",
                L"C:/workspace",
        }));
        require(set_cwd_result.has_value(), "servers set-cwd process should succeed");
        require(set_cwd_result->first == 0, "servers set-cwd exit code mismatch");
        require(set_cwd_result->second == "Set cxxmcp server filesystem cwd to C:/workspace\n",
                "servers set-cwd output mismatch");

        const auto set_env_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"servers",
                L"set-env",
                L"filesystem",
                L"API_TOKEN",
                L"secret",
        }));
        require(set_env_result.has_value(), "servers set-env process should succeed");
        require(set_env_result->first == 0, "servers set-env exit code mismatch");
        require(set_env_result->second == "Set cxxmcp server filesystem env API_TOKEN\n",
                "servers set-env output mismatch");

        const auto add_http_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"servers",
                L"add-http",
                L"remote",
                L"http://127.0.0.1:3000/mcp",
        }));
        require(add_http_result.has_value(), "servers add-http process should succeed");
        require(add_http_result->first == 0, "servers add-http exit code mismatch");
        require(add_http_result->second == "Added HTTP MCP server remote\n", "servers add-http output mismatch");

        const auto set_header_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"servers",
                L"set-header",
                L"remote",
                L"Authorization",
                L"\"Bearer token\"",
        }));
        require(set_header_result.has_value(), "servers set-header process should succeed");
        require(set_header_result->first == 0, "servers set-header exit code mismatch");
        require(set_header_result->second == "Set cxxmcp server remote header Authorization\n",
                "servers set-header output mismatch");

        const auto list_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"servers",
                L"list",
        }));
        require(list_result.has_value(), "servers list process should succeed");
        require(list_result->first == 0, "servers list exit code mismatch");
        require(list_result->second.find("filesystem\tfilesystem\tstdio\tenabled\tuntrusted") != std::string::npos,
                "servers list output mismatch");
        require(list_result->second.find("remote\tremote\tstreamable_http\tenabled\tuntrusted") != std::string::npos,
                "servers list added http output mismatch");

        const auto discover_all_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"servers",
                L"discover-all",
        }));
        require(discover_all_result.has_value(), "servers discover-all process should succeed");
        require(discover_all_result->first == 0, "servers discover-all exit code mismatch");
        require(discover_all_result->second.find("filesystem\tskipped\tcxxmcp server is untrusted: filesystem\n") !=
                        std::string::npos,
                "servers discover-all filesystem skip output mismatch");
        require(discover_all_result->second.find("remote\tskipped\tcxxmcp server is untrusted: remote\n") !=
                        std::string::npos,
                "servers discover-all remote skip output mismatch");

        const auto json_list_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"--json",
                L"servers",
                L"list",
        }));
        require(json_list_result.has_value(), "json servers list process should succeed");
        require(json_list_result->first == 0, "json servers list exit code mismatch");
        const auto listed_servers = Json::parse(json_list_result->second);
        require(listed_servers.is_array(), "json servers list should return an array");
        require(listed_servers.size() == 2, "json servers list count mismatch");
        require(listed_servers[0]["id"] == "filesystem", "json servers list id mismatch");
        require(listed_servers[0]["trust"] == "untrusted", "json servers list trust mismatch");

        const auto trust_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"servers",
                L"trust",
                L"filesystem",
        }));
        require(trust_result.has_value(), "servers trust process should succeed");
        require(trust_result->first == 0, "servers trust exit code mismatch");
        require(trust_result->second == "Set cxxmcp server filesystem trust to trusted\n",
                "servers trust output mismatch");

        const auto inspect_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"servers",
                L"inspect",
                L"filesystem",
        }));
        require(inspect_result.has_value(), "servers inspect process should succeed");
        require(inspect_result->first == 0, "servers inspect exit code mismatch");
        require(inspect_result->second.find("display_name: filesystem\n") != std::string::npos ||
                        inspect_result->second.find("display_name: Filesystem\n") != std::string::npos,
                "servers inspect output mismatch");
        require(inspect_result->second.find("stdio.cwd: C:/workspace\n") != std::string::npos,
                "servers inspect cwd output mismatch");
        require(inspect_result->second.find("  API_TOKEN=secret\n") != std::string::npos,
                "servers inspect env output mismatch");

        const auto disable_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"servers",
                L"disable",
                L"filesystem",
        }));
        require(disable_result.has_value(), "servers disable process should succeed");
        require(disable_result->first == 0, "servers disable exit code mismatch");
        require(disable_result->second == "Disabled cxxmcp server filesystem\n", "servers disable output mismatch");

        const auto controlled_list_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"servers",
                L"list",
        }));
        require(controlled_list_result.has_value(), "controlled servers list process should succeed");
        require(controlled_list_result->first == 0, "controlled servers list exit code mismatch");
        require(controlled_list_result->second.find("filesystem\tfilesystem\tstdio\tdisabled\ttrusted") !=
                        std::string::npos,
                "controlled servers list output mismatch");

        const auto inspect_remote_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"servers",
                L"inspect",
                L"remote",
        }));
        require(inspect_remote_result.has_value(), "remote inspect process should succeed");
        require(inspect_remote_result->first == 0, "remote inspect exit code mismatch");
        require(inspect_remote_result->second.find("  Authorization=Bearer token\n") != std::string::npos,
                "remote inspect header output mismatch");

        const auto remove_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"servers",
                L"remove",
                L"filesystem",
        }));
        require(remove_result.has_value(), "servers remove process should succeed");
        require(remove_result->first == 0, "servers remove exit code mismatch");
        require(remove_result->second == "Removed cxxmcp server filesystem\n", "servers remove output mismatch");

        const auto remove_remote_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"servers",
                L"remove",
                L"remote",
        }));
        require(remove_remote_result.has_value(), "servers remove remote process should succeed");
        require(remove_remote_result->first == 0, "servers remove remote exit code mismatch");
        require(remove_remote_result->second == "Removed cxxmcp server remote\n", "servers remove remote output mismatch");

        const auto removed_list_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"servers",
                L"list",
        }));
        require(removed_list_result.has_value(), "removed servers list process should succeed");
        require(removed_list_result->first == 0, "removed servers list exit code mismatch");
        require(removed_list_result->second == "No MCP servers configured\n", "removed servers list output mismatch");

        std::filesystem::remove_all(state_dir, ec);
#else
        require(false, "binary servers import test is only implemented on Windows");
#endif
    }

    void test_cli_binary_servers_add_http_trust_discovers_http_upstream() {
#ifdef _WIN32
        HttpUpstreamMcpFixture upstream;
        const auto cli = std::filesystem::path(MCP_TEST_CLI_EXE);
        const auto state_dir = std::filesystem::temp_directory_path() /
                               ("mcp-cli-binary-add-http-" + std::to_string(GetCurrentProcessId()) + "-" +
                                std::to_string(GetTickCount64()));
        std::error_code ec;
        std::filesystem::remove_all(state_dir, ec);
        std::filesystem::create_directories(state_dir, ec);
        require(!ec, "binary add-http setup should succeed");

        const auto upstream_url = upstream.url();
        const auto add_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"--json",
                L"servers",
                L"add-http",
                L"--trust",
                L"--discover",
                L"--header",
                L"Authorization",
                L"\"Bearer token\"",
                L"remote",
                std::wstring(upstream_url.begin(), upstream_url.end()),
        }));
        require(add_result.has_value(), "binary add-http trust discover process should succeed");
        require(add_result->first == 0, "binary add-http trust discover exit code mismatch");
        const auto added = Json::parse(add_result->second);
        require(added["id"] == "remote", "binary add-http trust discover id mismatch");
        require(added["trust"] == "trusted", "binary add-http trust discover trust mismatch");
        require(added["discovered"], "binary add-http trust discover flag mismatch");
        require(added["discoveredCapabilityCount"] == 3,
                "binary add-http trust discover count mismatch");
        require(added["http"]["headers"]["Authorization"] == "Bearer token",
                "binary add-http trust discover header mismatch");
        require(upstream.authorization_header_seen(),
                "binary add-http trust discover should send configured upstream header");

        const auto capabilities_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"capabilities",
                L"list",
        }));
        require(capabilities_result.has_value(), "binary add-http capabilities list process should succeed");
        require(capabilities_result->first == 0, "binary add-http capabilities list exit code mismatch");
        require(capabilities_result->second.find("remote:tool:echo\ttool\tremote\techo\tremote.echo\n") !=
                        std::string::npos,
                "binary add-http capabilities list tool mismatch");
        require(capabilities_result->second.find("remote:prompt:summarize\tprompt\tremote\tsummarize\tremote.summarize\n") !=
                        std::string::npos,
                "binary add-http capabilities list prompt mismatch");

        const auto inspect_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"servers",
                L"inspect",
                L"remote",
        }));
        require(inspect_result.has_value(), "binary add-http inspect process should succeed");
        require(inspect_result->first == 0, "binary add-http inspect exit code mismatch");
        require(inspect_result->second.find("trust: trusted\n") != std::string::npos,
                "binary add-http inspect trust mismatch");
        require(inspect_result->second.find("capabilities: 3 discovered\n") != std::string::npos,
                "binary add-http inspect capability count mismatch");
        require(inspect_result->second.find("  Authorization=Bearer token\n") != std::string::npos,
                "binary add-http inspect header mismatch");

        std::filesystem::remove_all(state_dir, ec);
#else
        require(false, "binary servers add-http discovery test is only implemented on Windows");
#endif
    }

    void test_cli_binary_servers_add_stdio_trust_discovers_child() {
#ifdef _WIN32
        const auto cli = std::filesystem::path(MCP_TEST_CLI_EXE);
        const auto child = std::filesystem::path(MCP_TEST_CHILD_EXE);
        const auto state_dir = std::filesystem::temp_directory_path() /
                               ("mcp-cli-binary-add-stdio-" + std::to_string(GetCurrentProcessId()) + "-" +
                                std::to_string(GetTickCount64()));
        const auto child_cwd = state_dir / "child-cwd";
        std::error_code ec;
        std::filesystem::remove_all(state_dir, ec);
        std::filesystem::create_directories(child_cwd, ec);
        require(!ec, "binary add-stdio setup should succeed");

        const auto add_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"--json",
                L"servers",
                L"add-stdio",
                L"--trust",
                L"--discover",
                L"--cwd",
                quote_wide(child_cwd),
                L"--env",
                L"API_TOKEN",
                L"secret",
                L"child",
                quote_wide(child),
        }));
        require(add_result.has_value(), "binary add-stdio trust discover process should succeed");
        require(add_result->first == 0, "binary add-stdio trust discover exit code mismatch");
        const auto added = Json::parse(add_result->second);
        require(added["id"] == "child", "binary add-stdio trust discover id mismatch");
        require(added["trust"] == "trusted", "binary add-stdio trust discover trust mismatch");
        require(added["discovered"], "binary add-stdio trust discover flag mismatch");
        require(added["discoveredCapabilityCount"] == 3,
                "binary add-stdio trust discover count mismatch");
        require(added["stdio"]["cwd"] == child_cwd.string(),
                "binary add-stdio trust discover cwd mismatch");
        require(added["stdio"]["env"]["API_TOKEN"] == "secret",
                "binary add-stdio trust discover env mismatch");

        const auto capabilities_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"capabilities",
                L"list",
        }));
        require(capabilities_result.has_value(), "binary add-stdio capabilities list process should succeed");
        require(capabilities_result->first == 0, "binary add-stdio capabilities list exit code mismatch");
        require(capabilities_result->second.find("child:tool:echo\ttool\tchild\techo\tchild.echo\n") !=
                        std::string::npos,
                "binary add-stdio capabilities list tool mismatch");
        require(capabilities_result->second.find("child:prompt:summarize\tprompt\tchild\tsummarize\tchild.summarize\n") !=
                        std::string::npos,
                "binary add-stdio capabilities list prompt mismatch");

        const auto inspect_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"servers",
                L"inspect",
                L"child",
        }));
        require(inspect_result.has_value(), "binary add-stdio inspect process should succeed");
        require(inspect_result->first == 0, "binary add-stdio inspect exit code mismatch");
        require(inspect_result->second.find("trust: trusted\n") != std::string::npos,
                "binary add-stdio inspect trust mismatch");
        require(inspect_result->second.find("capabilities: 3 discovered\n") != std::string::npos,
                "binary add-stdio inspect capability count mismatch");
        require(inspect_result->second.find("stdio.cwd: " + child_cwd.string() + "\n") != std::string::npos,
                "binary add-stdio inspect cwd mismatch");
        require(inspect_result->second.find("  API_TOKEN=secret\n") != std::string::npos,
                "binary add-stdio inspect env mismatch");

        std::filesystem::remove_all(state_dir, ec);
#else
        require(false, "binary servers add-stdio discovery test is only implemented on Windows");
#endif
    }

    void test_cli_binary_import_init_all_and_serve_all_child() {
#ifdef _WIN32
        const auto cli = std::filesystem::path(MCP_TEST_CLI_EXE);
        const auto child = std::filesystem::path(MCP_TEST_CHILD_EXE);
        const auto port = reserve_free_port();
        const auto state_dir = std::filesystem::temp_directory_path() /
                               ("mcp-cli-binary-import-init-all-" + std::to_string(GetCurrentProcessId()) + "-" +
                                std::to_string(GetTickCount64()));
        const auto config_path = state_dir / "client-config.json";
        std::error_code ec;
        std::filesystem::remove_all(state_dir, ec);
        std::filesystem::create_directories(state_dir, ec);
        require(!ec, "binary import init-all setup should succeed");

        {
            std::ofstream output(config_path);
            output << "{\n"
                   << "  \"mcpServers\": {\n"
                   << "    \"child\": {\n"
                   << "      \"command\": \"" << child.generic_string() << "\",\n"
                   << "      \"args\": []\n"
                   << "    }\n"
                   << "  }\n"
                   << "}\n";
        }

        const auto import_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"--json",
                L"servers",
                L"import",
                quote_wide(config_path),
        }));
        require(import_result.has_value(), "binary import init-all import process should succeed");
        require(import_result->first == 0, "binary import init-all import exit code mismatch");
        const auto imported = Json::parse(import_result->second);
        require(imported["importedCount"] == 1, "binary import init-all imported count mismatch");
        require(!imported["trusted"], "binary import init-all import should not trust by default");
        require(!imported["discovered"], "binary import init-all import should not discover by default");
        require(imported["discoveredCapabilityCount"] == 0,
                "binary import init-all import discovery count mismatch");

        const auto init_all_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"--json",
                L"gateway",
                L"init-all",
                L"--trust",
                L"--discover",
                L"--instructions",
                L"\"Use imported child tools.\"",
                L"127.0.0.1",
                std::to_wstring(port),
                L"/cxxmcp/imported",
        }));
        require(init_all_result.has_value(), "binary import init-all gateway process should succeed");
        require(init_all_result->first == 0, "binary import init-all gateway exit code mismatch");
        const auto initialized = Json::parse(init_all_result->second);
        require(initialized["trusted"], "binary import init-all trust flag mismatch");
        require(initialized["trustedServerCount"] == 1, "binary import init-all trusted count mismatch");
        require(initialized["discovered"], "binary import init-all discovery flag mismatch");
        require(initialized["discoveredCapabilityCount"] == 3,
                "binary import init-all discovery count mismatch");
        require(initialized["initializedCount"] == 1, "binary import init-all initialized count mismatch");
        require(initialized["skippedCount"] == 0, "binary import init-all skipped count mismatch");
        require(initialized["profiles"][0]["profileId"] == "profile.child",
                "binary import init-all profile id mismatch");
        require(initialized["profiles"][0]["url"] ==
                        ("http://127.0.0.1:" + std::to_string(port) + "/cxxmcp/imported/child"),
                "binary import init-all endpoint url mismatch");

        const auto config_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"gateway",
                L"client-config-all",
                L"local",
        }));
        require(config_result.has_value(), "binary import init-all client-config-all process should succeed");
        require(config_result->first == 0, "binary import init-all client-config-all exit code mismatch");
        const auto config = Json::parse(config_result->second);
        require(config["mcpServers"]["local.profile.child"]["url"] ==
                        ("http://127.0.0.1:" + std::to_string(port) + "/cxxmcp/imported/child"),
                "binary import init-all client-config-all url mismatch");

        const auto serve_command = make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"gateway",
                L"serve-all",
        });
        auto served = start_process(serve_command);
        require(served.has_value(), "binary import init-all serve-all process should start");
        struct ProcessCleanup {
            PROCESS_INFORMATION &process;
            ~ProcessCleanup() {
                if (process.hProcess != nullptr) {
                    TerminateProcess(process.hProcess, 0);
                    WaitForSingleObject(process.hProcess, 5000);
                    close_process(process);
                }
            }
        } cleanup{*served};
        require(wait_for_http_gateway(port, "/cxxmcp/imported/child"),
                "binary import init-all gateway should become reachable");

        mcp::client::Client client(std::make_unique<mcp::client::HttpTransport>(
                mcp::client::HttpTransportOptions{
                        .host = "127.0.0.1",
                        .port = port,
                        .path = "/cxxmcp/imported/child",
                        .headers = {},
                        .timeout = std::chrono::milliseconds{1000},
                }));

        const auto initialized_response = client.raw_request(mcp::protocol::make_initialize_request(std::int64_t{1}));
        require(initialized_response.has_value(), "binary import init-all initialize should succeed");
        require(initialized_response->at("instructions") == "Use imported child tools.",
                "binary import init-all initialize instructions mismatch");

        const auto tools = client.list_tools();
        require(tools.has_value(), "binary import init-all tools/list should succeed");
        require(tools->size() == 1 && tools->front().name == "child.echo",
                "binary import init-all tools/list mismatch");

        const auto called = client.call_tool(mcp::protocol::ToolCall{
                .name = "child.echo",
                .arguments = Json{{"message", "hello"}},
        });
        require(called.has_value(), "binary import init-all tools/call should succeed");
        require(called->structured_content.has_value() &&
                        called->structured_content->value("message", "") == "hello",
                "binary import init-all tools/call structured content mismatch");

        std::filesystem::remove_all(state_dir, ec);
#else
        require(false, "binary import init-all serve-all test is only implemented on Windows");
#endif
    }

    void test_cli_binary_gateway_import_config_initializes_child() {
#ifdef _WIN32
        const auto cli = std::filesystem::path(MCP_TEST_CLI_EXE);
        const auto child = std::filesystem::path(MCP_TEST_CHILD_EXE);
        const auto port = reserve_free_port();
        const auto state_dir = std::filesystem::temp_directory_path() /
                               ("mcp-cli-binary-gateway-import-config-" + std::to_string(GetCurrentProcessId()) + "-" +
                                std::to_string(GetTickCount64()));
        const auto config_path = state_dir / "client-config.json";
        std::error_code ec;
        std::filesystem::remove_all(state_dir, ec);
        std::filesystem::create_directories(state_dir, ec);
        require(!ec, "binary gateway import-config setup should succeed");

        {
            std::ofstream output(config_path);
            output << "{\n"
                   << "  \"mcpServers\": {\n"
                   << "    \"child\": {\n"
                   << "      \"command\": \"" << child.generic_string() << "\",\n"
                   << "      \"args\": []\n"
                   << "    }\n"
                   << "  }\n"
                   << "}\n";
        }

        const auto import_config_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"--json",
                L"gateway",
                L"import-config",
                L"--trust",
                L"--discover",
                L"--instructions",
                L"\"Use one-step imported child tools.\"",
                quote_wide(config_path),
                L"127.0.0.1",
                std::to_wstring(port),
                L"/cxxmcp/one-step",
        }));
        require(import_config_result.has_value(), "binary gateway import-config process should succeed");
        require(import_config_result->first == 0, "binary gateway import-config exit code mismatch");
        const auto imported = Json::parse(import_config_result->second);
        require(imported["importedCount"] == 1, "binary gateway import-config imported count mismatch");
        require(imported["trusted"], "binary gateway import-config trust flag mismatch");
        require(imported["discovered"], "binary gateway import-config discovery flag mismatch");
        require(imported["discoveredCapabilityCount"] == 3,
                "binary gateway import-config discovery count mismatch");
        require(imported["initializedCount"] == 1, "binary gateway import-config initialized count mismatch");
        require(imported["skippedCount"] == 0, "binary gateway import-config skipped count mismatch");
        require(imported["profiles"][0]["profileId"] == "profile.child",
                "binary gateway import-config profile id mismatch");
        require(imported["profiles"][0]["url"] ==
                        ("http://127.0.0.1:" + std::to_string(port) + "/cxxmcp/one-step/child"),
                "binary gateway import-config endpoint url mismatch");

        const auto check_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"gateway",
                L"check",
                L"profile.child",
        }));
        require(check_result.has_value(), "binary gateway import-config check process should succeed");
        require(check_result->first == 0, "binary gateway import-config check exit code mismatch");
        require(check_result->second == "profile.child\tready\t3 enabled binding(s)\n",
                "binary gateway import-config check output mismatch");

        const auto config_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"gateway",
                L"client-config-all",
                L"local",
        }));
        require(config_result.has_value(), "binary gateway import-config client-config-all process should succeed");
        require(config_result->first == 0, "binary gateway import-config client-config-all exit code mismatch");
        const auto config = Json::parse(config_result->second);
        require(config["mcpServers"]["local.profile.child"]["url"] ==
                        ("http://127.0.0.1:" + std::to_string(port) + "/cxxmcp/one-step/child"),
                "binary gateway import-config client-config-all url mismatch");

        std::filesystem::remove_all(state_dir, ec);
#else
        require(false, "binary gateway import-config test is only implemented on Windows");
#endif
    }

    void test_cli_binary_gateway_init_http_onboarding_with_http_upstream() {
#ifdef _WIN32
        HttpUpstreamMcpFixture upstream;
        const auto cli = std::filesystem::path(MCP_TEST_CLI_EXE);
        const auto state_dir = std::filesystem::temp_directory_path() /
                               ("mcp-cli-binary-init-http-" + std::to_string(GetCurrentProcessId()) + "-" +
                                std::to_string(GetTickCount64()));
        const auto gateway_port = reserve_free_port();
        std::error_code ec;
        std::filesystem::remove_all(state_dir, ec);
        std::filesystem::create_directories(state_dir, ec);
        require(!ec, "binary init-http setup should succeed");

        const auto upstream_url = upstream.url();
        const auto init_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"gateway",
                L"init-http",
                L"--trust",
                L"--discover",
                L"--path",
                L"/cxxmcp/http",
                L"--instructions",
                L"\"Use HTTP upstream tools.\"",
                L"--header",
                L"Authorization",
                L"\"Bearer token\"",
                L"profile.http",
                L"remote",
                L"127.0.0.1",
                std::to_wstring(gateway_port),
                std::wstring(upstream_url.begin(), upstream_url.end()),
        }));
        require(init_result.has_value(), "binary init-http process should succeed");
        require(init_result->first == 0, "binary init-http exit code mismatch");
        require(init_result->second.find("Initialized gateway profile profile.http for remote\n") !=
                        std::string::npos,
                "binary init-http output header mismatch");
        require(init_result->second.find("Trust: trusted\n") != std::string::npos,
                "binary init-http trust output mismatch");
        require(init_result->second.find("Discovered 3 capability(s)\n") != std::string::npos,
                "binary init-http discovery output mismatch");
        require(init_result->second.find("Instructions: set\n") != std::string::npos,
                "binary init-http instructions output mismatch");
        require(init_result->second.find("Bindings: 3 added, 0 refreshed\n") != std::string::npos,
                "binary init-http binding output mismatch");
        require(upstream.authorization_header_seen(), "binary init-http should send configured upstream header");

        const auto check_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"gateway",
                L"check",
                L"profile.http",
        }));
        require(check_result.has_value(), "binary init-http check process should succeed");
        require(check_result->first == 0, "binary init-http check exit code mismatch");
        require(check_result->second == "profile.http\tready\t3 enabled binding(s)\n",
                "binary init-http check output mismatch");

        const auto config_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"gateway",
                L"client-config",
                L"profile.http",
                L"http-gateway",
        }));
        require(config_result.has_value(), "binary init-http client-config process should succeed");
        require(config_result->first == 0, "binary init-http client-config exit code mismatch");
        const auto config = Json::parse(config_result->second);
        require(config["mcpServers"]["http-gateway"]["url"] ==
                        "http://127.0.0.1:" + std::to_string(gateway_port) + "/cxxmcp/http",
                "binary init-http client-config url mismatch");

        const auto inspect_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"servers",
                L"inspect",
                L"remote",
        }));
        require(inspect_result.has_value(), "binary init-http inspect process should succeed");
        require(inspect_result->first == 0, "binary init-http inspect exit code mismatch");
        require(inspect_result->second.find("transport: streamable_http\n") != std::string::npos,
                "binary init-http inspect transport mismatch");
        require(inspect_result->second.find("  Authorization=Bearer token\n") != std::string::npos,
                "binary init-http inspect header mismatch");

        const auto serve_command = make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"gateway",
                L"serve-http",
                L"profile.http",
        });
        auto served = start_process(serve_command);
        require(served.has_value(), "binary init-http serve-http process should start");
        struct ProcessCleanup {
            PROCESS_INFORMATION &process;
            ~ProcessCleanup() {
                if (process.hProcess != nullptr) {
                    TerminateProcess(process.hProcess, 0);
                    WaitForSingleObject(process.hProcess, 5000);
                    close_process(process);
                }
            }
        } cleanup{*served};
        require(wait_for_http_gateway(gateway_port, "/cxxmcp/http"),
                "binary init-http gateway should become reachable");

        mcp::client::Client client(std::make_unique<mcp::client::HttpTransport>(
                mcp::client::HttpTransportOptions{
                        .host = "127.0.0.1",
                        .port = gateway_port,
                        .path = "/cxxmcp/http",
                        .headers = {},
                        .timeout = std::chrono::milliseconds{1000},
                }));

        const auto initialized = client.raw_request(mcp::protocol::make_initialize_request(std::int64_t{7}));
        require(initialized.has_value(), "binary init-http initialize should succeed");
        require(initialized->at("instructions") == "Use HTTP upstream tools.",
                "binary init-http initialize instructions mismatch");

        const auto called = client.call_tool(mcp::protocol::ToolCall{
                .name = "remote.echo",
                .arguments = Json{{"value", 42}},
        });
        require(called.has_value(), "binary init-http gateway should route tools/call to http upstream");
        require(!called->content.empty() && called->content.front().text == "http upstream",
                "binary init-http upstream result mismatch");
        require(called->structured_content.has_value(), "binary init-http structured content missing");
        require(called->structured_content->at("value") == 42, "binary init-http arguments mismatch");

        std::filesystem::remove_all(state_dir, ec);
#else
        require(false, "binary gateway init-http test is only implemented on Windows");
#endif
    }

    void test_cli_binary_gateway_onboarding_with_stdio_child() {
#ifdef _WIN32
        const auto cli = std::filesystem::path(MCP_TEST_CLI_EXE);
        const auto child = std::filesystem::path(MCP_TEST_CHILD_EXE);
        const auto state_dir = std::filesystem::temp_directory_path() /
                               ("mcp-cli-binary-onboarding-" + std::to_string(GetCurrentProcessId()) + "-" +
                                std::to_string(GetTickCount64()));
        std::error_code ec;
        std::filesystem::remove_all(state_dir, ec);
        std::filesystem::create_directories(state_dir, ec);
        require(!ec, "binary onboarding setup should succeed");

        const auto init_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"gateway",
                L"init-stdio",
                L"--trust",
                L"--discover",
                L"--path",
                L"/cxxmcp/child",
                L"--instructions",
                L"\"Use only reviewed child tools.\"",
                L"profile.child",
                L"child",
                L"127.0.0.1",
                L"39941",
                quote_wide(child),
        }));
        require(init_result.has_value(), "binary onboarding gateway init-stdio process should succeed");
        require(init_result->first == 0, "binary onboarding gateway init-stdio exit code mismatch");
        require(init_result->second.find("Trust: trusted\n") != std::string::npos,
                "binary onboarding gateway init-stdio trust output mismatch");
        require(init_result->second.find("Discovered 3 capability(s)\n") != std::string::npos,
                "binary onboarding gateway init-stdio discovery output mismatch");
        require(init_result->second.find("Instructions: set\n") != std::string::npos,
                "binary onboarding gateway init-stdio instructions output mismatch");
        require(init_result->second.find("Bindings: 3 added, 0 refreshed\n") != std::string::npos,
                "binary onboarding gateway init-stdio binding output mismatch");
        require(init_result->second.find("Next: cxxmcp gateway check profile.child\n") != std::string::npos,
                "binary onboarding gateway init-stdio next check output mismatch");
        require(init_result->second.find("Next: cxxmcp gateway client-config profile.child\n") != std::string::npos,
                "binary onboarding gateway init-stdio next client-config output mismatch");
        require(init_result->second.find("Next: cxxmcp gateway serve-http profile.child\n") != std::string::npos,
                "binary onboarding gateway init-stdio next serve-http output mismatch");

        const auto check_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"gateway",
                L"check",
                L"profile.child",
        }));
        require(check_result.has_value(), "binary onboarding gateway check process should succeed");
        require(check_result->first == 0, "binary onboarding gateway check exit code mismatch");
        require(check_result->second == "profile.child\tready\t3 enabled binding(s)\n",
                "binary onboarding gateway check output mismatch");

        const auto config_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"gateway",
                L"client-config",
                L"profile.child",
                L"child-gateway",
        }));
        require(config_result.has_value(), "binary onboarding client-config process should succeed");
        require(config_result->first == 0, "binary onboarding client-config exit code mismatch");
        const auto config = Json::parse(config_result->second);
        require(config["mcpServers"]["child-gateway"]["url"] == "http://127.0.0.1:39941/cxxmcp/child",
                "binary onboarding client-config url mismatch");

        const auto preview_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"gateway",
                L"preview",
                L"profile.child",
        }));
        require(preview_result.has_value(), "binary onboarding gateway preview process should succeed");
        require(preview_result->first == 0, "binary onboarding gateway preview exit code mismatch");
        require(preview_result->second.find("tools:\n- child.echo\tEcho test tool\n") != std::string::npos,
                "binary onboarding gateway preview tool mismatch");
        require(preview_result->second.find("prompts:\n- child.summarize\tSummarize test prompt\n") !=
                        std::string::npos,
                "binary onboarding gateway preview prompt mismatch");
        require(preview_result->second.find("resources:\n- file:///workspace/README.md\tchild.Readme\tWorkspace readme\n") !=
                        std::string::npos,
                "binary onboarding gateway preview resource mismatch");

        const std::string stdio_input =
                R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})"
                "\n"
                R"({"jsonrpc":"2.0","method":"notifications/initialized","params":{}})"
                "\n"
                R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})"
                "\n"
                R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"child.echo","arguments":{"message":"hello"}}})"
                "\n"
                R"({"jsonrpc":"2.0","id":4,"method":"prompts/get","params":{"name":"child.summarize","arguments":{"text":"hello"}}})"
                "\n"
                R"({"jsonrpc":"2.0","id":5,"method":"resources/read","params":{"uri":"file:///workspace/README.md"}})"
                "\n";
        const auto stdio_result = run_process_capture(make_command_line({
                                                              quote_wide(cli),
                                                              L"--state-dir",
                                                              quote_wide(state_dir),
                                                              L"gateway",
                                                              L"serve-stdio",
                                                              L"profile.child",
                                                      }),
                                                      stdio_input);
        require(stdio_result.has_value(), "binary onboarding serve-stdio process should succeed");
        require(stdio_result->first == 0, "binary onboarding serve-stdio exit code mismatch");
        require(stdio_result->second.find("\"instructions\":\"Use only reviewed child tools.\"") != std::string::npos,
                "binary onboarding serve-stdio initialize instructions mismatch");
        require(stdio_result->second.find("\"name\":\"child.echo\"") != std::string::npos,
                "binary onboarding serve-stdio tools/list mismatch");
        require(stdio_result->second.find("\"id\":3") != std::string::npos &&
                        stdio_result->second.find("\"text\":\"echo\"") != std::string::npos &&
                        stdio_result->second.find("\"message\":\"hello\"") != std::string::npos,
                "binary onboarding serve-stdio tools/call mismatch");
        require(stdio_result->second.find("\"id\":4") != std::string::npos &&
                        stdio_result->second.find("\"text\":\"Summarize hello\"") != std::string::npos,
                "binary onboarding serve-stdio prompts/get mismatch");
        require(stdio_result->second.find("\"id\":5") != std::string::npos &&
                        stdio_result->second.find("\"text\":\"hello from readme\"") != std::string::npos,
                "binary onboarding serve-stdio resources/read mismatch");

        const auto stdio_config_result = run_process_capture(make_command_line({
                quote_wide(cli),
                L"--state-dir",
                quote_wide(state_dir),
                L"gateway",
                L"client-config-stdio",
                L"profile.child",
                L"generated-gateway",
        }));
        require(stdio_config_result.has_value(), "binary onboarding client-config-stdio process should succeed");
        require(stdio_config_result->first == 0, "binary onboarding client-config-stdio exit code mismatch");
        const auto stdio_config = Json::parse(stdio_config_result->second);
        const auto &generated_server = stdio_config["mcpServers"]["generated-gateway"];
        std::vector<std::wstring> generated_command{
                quote_wide(std::filesystem::path(generated_server["command"].get<std::string>())),
        };
        for (const auto &arg: generated_server["args"]) {
            generated_command.push_back(L"\"" + std::filesystem::path(arg.get<std::string>()).wstring() + L"\"");
        }

        const std::string generated_stdio_input =
                R"({"jsonrpc":"2.0","id":10,"method":"initialize","params":{}})"
                "\n"
                R"({"jsonrpc":"2.0","id":11,"method":"tools/list","params":{}})"
                "\n";
        const auto generated_stdio_result =
                run_process_capture(make_command_line(generated_command), generated_stdio_input);
        require(generated_stdio_result.has_value(), "generated stdio client config process should succeed");
        require(generated_stdio_result->first == 0, "generated stdio client config exit code mismatch");
        require(generated_stdio_result->second.find("\"instructions\":\"Use only reviewed child tools.\"") !=
                        std::string::npos,
                "generated stdio client config initialize instructions mismatch");
        require(generated_stdio_result->second.find("\"name\":\"child.echo\"") != std::string::npos,
                "generated stdio client config tools/list mismatch");

        std::filesystem::remove_all(state_dir, ec);
#else
        require(false, "binary gateway onboarding test is only implemented on Windows");
#endif
    }

}// namespace

int main() {
    const std::vector<std::pair<std::string_view, void (*)()>> tests = {
            {"lists tools", test_lists_tools},
            {"enables and disables tool policy", test_enables_and_disables_tool_policy},
            {"lists profiles", test_lists_profiles},
            {"exports bundle through service", test_exports_bundle_through_service},
            {"imports bundle through service", test_imports_bundle_through_service},
            {"doctor reports empty runtime", test_doctor_reports_empty_runtime},
            {"doctor reports ready runtime as json", test_doctor_reports_ready_runtime_as_json},
            {"doctor reports endpointless gateway profile as not ready",
             test_doctor_reports_endpointless_gateway_profile_as_not_ready},
            {"doctor reports unready upstream health as json", test_doctor_reports_unready_upstream_health_as_json},
            {"lists cxxmcp servers", test_lists_mcp_servers},
            {"lists cxxmcp servers as json", test_lists_mcp_servers_as_json},
            {"inspects cxxmcp server", test_inspects_mcp_server},
            {"inspects cxxmcp server as json", test_inspects_mcp_server_as_json},
            {"inspects cxxmcp server usage context", test_inspects_mcp_server_usage_context},
            {"inspects cxxmcp server usage context as json", test_inspects_mcp_server_usage_context_as_json},
            {"imports cxxmcp servers from client config", test_imports_mcp_servers_from_client_config},
            {"imports cxxmcp servers from client config as json", test_imports_mcp_servers_from_client_config_as_json},
            {"imports cxxmcp servers with trust and discovery as json",
             test_imports_mcp_servers_with_trust_and_discovery_as_json},
            {"adds stdio cxxmcp server", test_adds_stdio_mcp_server},
            {"adds stdio cxxmcp server as json", test_adds_stdio_mcp_server_as_json},
            {"adds stdio cxxmcp server with launch options as json",
             test_adds_stdio_mcp_server_with_launch_options_as_json},
            {"adds http cxxmcp server", test_adds_http_mcp_server},
            {"adds http cxxmcp server as json", test_adds_http_mcp_server_as_json},
            {"adds http cxxmcp server with headers as json", test_adds_http_mcp_server_with_headers_as_json},
            {"configures stdio cxxmcp server launch environment", test_configures_stdio_mcp_server_launch_environment},
            {"configures stdio cxxmcp server launch environment as json",
             test_configures_stdio_mcp_server_launch_environment_as_json},
            {"configures http cxxmcp server headers", test_configures_http_mcp_server_headers},
            {"configures http cxxmcp server headers as json", test_configures_http_mcp_server_headers_as_json},
            {"updates cxxmcp server control state", test_updates_mcp_server_control_state},
            {"updates cxxmcp server control state as json", test_updates_mcp_server_control_state_as_json},
            {"discovers cxxmcp server capabilities", test_discovers_mcp_server_capabilities},
            {"discovers cxxmcp server capabilities as json", test_discovers_mcp_server_capabilities_as_json},
            {"discover cxxmcp server reports trust hint", test_discover_mcp_server_reports_trust_hint},
            {"discovers all cxxmcp servers with skips", test_discovers_all_mcp_servers_with_skips},
            {"discovers all cxxmcp servers as json", test_discovers_all_mcp_servers_as_json},
            {"checks cxxmcp server health", test_checks_mcp_server_health},
            {"checks cxxmcp server health as json", test_checks_mcp_server_health_as_json},
            {"checks all cxxmcp servers health", test_checks_all_mcp_servers_health},
            {"checks all cxxmcp servers health as json", test_checks_all_mcp_servers_health_as_json},
            {"lists discovered capabilities", test_lists_discovered_capabilities},
            {"lists discovered capabilities as json", test_lists_discovered_capabilities_as_json},
            {"inspects discovered capability", test_inspects_discovered_capability},
            {"inspects discovered capability as json", test_inspects_discovered_capability_as_json},
            {"creates and lists exposure profiles", test_creates_and_lists_exposure_profiles},
            {"gateway lists and inspects profiles", test_gateway_lists_and_inspects_profiles},
            {"gateway lists and inspects profiles as json", test_gateway_lists_and_inspects_profiles_as_json},
            {"creates and configures exposure profile as json", test_creates_and_configures_exposure_profile_as_json},
            {"binds capability to exposure profile", test_binds_capability_to_exposure_profile},
            {"binds capability to exposure profile as json", test_binds_capability_to_exposure_profile_as_json},
            {"sets exposure profile instructions", test_sets_exposure_profile_instructions},
            {"binds server capabilities to exposure profile", test_binds_server_capabilities_to_exposure_profile},
            {"binds server capabilities to exposure profile as json",
             test_binds_server_capabilities_to_exposure_profile_as_json},
            {"enables and disables exposure binding", test_enables_and_disables_exposure_binding},
            {"enables disables and unbinds exposure binding as json",
             test_enables_disables_and_unbinds_exposure_binding_as_json},
            {"prunes stale exposure bindings", test_prunes_stale_exposure_bindings},
            {"prunes stale exposure bindings as json", test_prunes_stale_exposure_bindings_as_json},
            {"inspects unbinds and removes exposure profile", test_inspects_unbinds_and_removes_exposure_profile},
            {"inspects exposure profile as json", test_inspects_exposure_profile_as_json},
            {"writes gateway client config", test_writes_gateway_client_config},
            {"gateway client-config requires ready profile",
             test_gateway_client_config_requires_ready_profile},
            {"writes gateway all client config", test_writes_gateway_all_client_config},
            {"gateway client-config-all requires ready profiles",
             test_gateway_all_client_config_requires_ready_profiles},
            {"writes gateway ready-only client config", test_writes_gateway_ready_only_client_config},
            {"gateway ready-only client config skips runtime-unready profile",
             test_gateway_ready_only_client_config_skips_runtime_unready_profile},
            {"writes gateway stdio client config", test_writes_gateway_stdio_client_config},
            {"gateway stdio client-config requires ready bindings",
             test_gateway_stdio_client_config_requires_ready_bindings},
            {"initializes gateway profile from server", test_initializes_gateway_profile_from_server},
            {"initializes gateway profile from server as json", test_initializes_gateway_profile_from_server_as_json},
            {"gateway init reuses existing profile", test_gateway_init_reuses_existing_profile},
            {"gateway init refreshes existing bindings", test_gateway_init_refreshes_existing_bindings},
            {"gateway init refreshes existing bindings as json", test_gateway_init_refreshes_existing_bindings_as_json},
            {"gateway init without capabilities reports discovery hint",
             test_gateway_init_without_capabilities_reports_discovery_hint},
            {"initializes gateway profile with discovery", test_initializes_gateway_profile_with_discovery},
            {"gateway init discover reports trust hint", test_gateway_init_discover_reports_trust_hint},
            {"gateway init trusts and discovers server", test_gateway_init_trusts_and_discovers_server},
            {"initializes gateway profile with discovery as json", test_initializes_gateway_profile_with_discovery_as_json},
            {"gateway init trusts and discovers server as json", test_gateway_init_trusts_and_discovers_server_as_json},
            {"initializes gateway profile with stdio server onboarding",
             test_initializes_gateway_profile_with_stdio_server_onboarding},
            {"initializes gateway profile with stdio server onboarding as json",
             test_initializes_gateway_profile_with_stdio_server_onboarding_as_json},
            {"initializes gateway profile with http server onboarding",
             test_initializes_gateway_profile_with_http_server_onboarding},
            {"initializes gateway profile with http server onboarding as json",
             test_initializes_gateway_profile_with_http_server_onboarding_as_json},
            {"gateway onboarding one-shot flow", test_gateway_onboarding_one_shot_flow},
            {"gateway init-all initializes discovered servers as json",
             test_gateway_init_all_initializes_discovered_servers_as_json},
            {"gateway init-all prints ready-only next steps",
             test_gateway_init_all_prints_ready_only_next_steps},
            {"gateway init-all trusts and discovers servers as json",
             test_gateway_init_all_trusts_and_discovers_servers_as_json},
            {"gateway import-config trusts discovers and initializes as json",
             test_gateway_import_config_trusts_discovers_and_initializes_as_json},
            {"checks ready gateway profile", test_checks_ready_gateway_profile},
            {"gateway check requires http endpoint", test_gateway_check_requires_http_endpoint},
            {"checks gateway profile as json", test_checks_gateway_profile_as_json},
            {"gateway check reports runtime-unready upstream as json",
             test_gateway_check_reports_runtime_unready_upstream_as_json},
            {"gateway status reports runtime-unready upstream as json",
             test_gateway_status_reports_runtime_unready_upstream_as_json},
            {"checks all gateway profiles", test_checks_all_gateway_profiles},
            {"checks all gateway profiles as json", test_checks_all_gateway_profiles_as_json},
            {"gateway status summarizes HTTP client readiness",
             test_gateway_status_summarizes_http_client_readiness},
            {"gateway status reports json", test_gateway_status_reports_json},
            {"previews gateway exposed capabilities", test_previews_gateway_exposed_capabilities},
            {"previews gateway exposed capabilities as json", test_previews_gateway_exposed_capabilities_as_json},
            {"serves gateway tools/list over stdio", test_serves_gateway_tools_list_over_stdio},
            {"gateway serve-stdio rejects unready profile bindings",
             test_gateway_serve_stdio_rejects_unready_profile_bindings},
            {"gateway serve-stdio rejects empty profile bindings",
             test_gateway_serve_stdio_rejects_empty_profile_bindings},
            {"gateway serve-http requires configured endpoint", test_gateway_serve_http_requires_configured_endpoint},
            {"gateway serve-all requires profiles and endpoints",
             test_gateway_serve_all_requires_profiles_and_endpoints},
            {"gateway serve-http end to end", test_gateway_serve_http_end_to_end},
            {"cli binary gateway serve-http proxies stdio child",
             test_cli_binary_gateway_serve_http_proxies_stdio_child},
            {"cli binary gateway serve-http isolates profiles",
             test_cli_binary_gateway_serve_http_isolates_profiles},
            {"invalid command returns usage error", test_invalid_command_returns_usage_error},
            {"command group help outputs scoped usage", test_command_group_help_outputs_scoped_usage},
            {"runtime options parse state dir and version", test_runtime_options_parse_state_dir_and_version},
            {"runtime options parse equals form and short version", test_runtime_options_parse_equals_form_and_short_version},
            {"runtime options parse json output", test_runtime_options_parse_json_output},
            {"runtime options parse global help", test_runtime_options_parse_global_help},
            {"runtime options preserves command help", test_runtime_options_preserves_command_help},
            {"runtime options rejects missing state dir value", test_runtime_options_rejects_missing_state_dir_value},
            {"cli binary help and version output", test_cli_binary_help_and_version_output},
            {"cli binary state dir override", test_cli_binary_state_dir_override},
            {"cli binary creates missing state dir", test_cli_binary_creates_missing_state_dir},
            {"cli binary uses runtime home environment", test_cli_binary_uses_runtime_home_environment},
            {"cli binary servers import and list", test_cli_binary_servers_import_and_list},
            {"cli binary servers add-http trust discovers http upstream",
             test_cli_binary_servers_add_http_trust_discovers_http_upstream},
            {"cli binary servers add-stdio trust discovers child",
             test_cli_binary_servers_add_stdio_trust_discovers_child},
            {"cli binary import init-all and serve-all child",
             test_cli_binary_import_init_all_and_serve_all_child},
            {"cli binary gateway import-config initializes child",
             test_cli_binary_gateway_import_config_initializes_child},
            {"cli binary gateway init-http onboarding with http upstream",
             test_cli_binary_gateway_init_http_onboarding_with_http_upstream},
            {"cli binary gateway init-stdio onboarding with stdio child",
             test_cli_binary_gateway_onboarding_with_stdio_child},
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
