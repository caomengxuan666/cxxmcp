#include "mcp/client/process_stdio_transport.hpp"

#include "mcp/protocol/serialization.hpp"

#include <algorithm>
#include <cwchar>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace mcp::client {
namespace {

core::Error make_process_error(int code, std::string message, std::string detail = {}) {
    return core::Error{code, std::move(message), std::move(detail)};
}

core::Error make_process_error(std::string message, std::string detail = {}) {
    return make_process_error(static_cast<int>(protocol::ErrorCode::InternalError),
                              std::move(message),
                              std::move(detail));
}

#ifdef _WIN32

std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string windows_error_message(DWORD error) {
    LPWSTR buffer = nullptr;
    const DWORD size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                          FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr,
                                      error,
                                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                      reinterpret_cast<LPWSTR>(&buffer),
                                      0,
                                      nullptr);
    if (size == 0 || !buffer) {
        return "Windows error " + std::to_string(error);
    }

    std::wstring wide(buffer, size);
    LocalFree(buffer);
    const int utf8_size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
                                             nullptr, nullptr);
    std::string result(static_cast<std::size_t>(utf8_size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), utf8_size, nullptr,
                        nullptr);
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n')) {
        result.pop_back();
    }
    return result;
}

std::wstring quote_arg(const std::wstring& arg) {
    if (arg.empty()) {
        return L"\"\"";
    }
    const bool needs_quotes = arg.find_first_of(L" \t\n\v\"") != std::wstring::npos;
    if (!needs_quotes) {
        return arg;
    }

    std::wstring result = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(ch);
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(ch);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

std::wstring command_line_from_options(const ProcessStdioTransportOptions& options) {
    std::wstring command_line = quote_arg(utf8_to_wide(options.command));
    for (const auto& arg : options.args) {
        command_line.push_back(L' ');
        command_line += quote_arg(utf8_to_wide(arg));
    }
    return command_line;
}

std::vector<wchar_t> environment_block(const ProcessStdioTransportOptions& options) {
    if (options.env.empty()) {
        return {};
    }

    std::vector<std::wstring> entries;
    if (LPWCH current = GetEnvironmentStringsW()) {
        for (LPWCH item = current; *item != L'\0'; item += std::wcslen(item) + 1) {
            entries.emplace_back(item);
        }
        FreeEnvironmentStringsW(current);
    }

    for (const auto& [key, value] : options.env) {
        const auto wide_key = utf8_to_wide(key);
        const auto prefix = wide_key + L"=";
        const auto it = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
            return _wcsnicmp(entry.c_str(), prefix.c_str(), prefix.size()) == 0;
        });
        if (it == entries.end()) {
            entries.push_back(prefix + utf8_to_wide(value));
        } else {
            *it = prefix + utf8_to_wide(value);
        }
    }

    std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
        return _wcsicmp(lhs.c_str(), rhs.c_str()) < 0;
    });

    std::vector<wchar_t> block;
    for (const auto& entry : entries) {
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

class Handle final {
public:
    Handle() = default;
    explicit Handle(HANDLE handle)
        : handle_(handle) {}
    ~Handle() {
        reset();
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    Handle(Handle&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}

    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    HANDLE get() const noexcept {
        return handle_;
    }

    HANDLE release() noexcept {
        return std::exchange(handle_, nullptr);
    }

    void reset(HANDLE handle = nullptr) noexcept {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

    explicit operator bool() const noexcept {
        return handle_ && handle_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE handle_ = nullptr;
};

#endif

} // namespace

class ProcessStdioTransport::Impl {
public:
    explicit Impl(ProcessStdioTransportOptions options)
        : options_(std::move(options)) {}

    ~Impl() {
        stop();
    }

    core::Result<core::Unit> write_line(const std::string& line) {
        const auto started = ensure_started();
        if (!started) {
            return std::unexpected(started.error());
        }

#ifdef _WIN32
        const std::string payload = line + "\n";
        DWORD written = 0;
        if (!WriteFile(stdin_write_.get(), payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr) ||
            written != payload.size()) {
            return std::unexpected(make_process_error("failed to write process stdin",
                                                      windows_error_message(GetLastError())));
        }
        return core::Unit{};
#else
        return std::unexpected(make_process_error("process stdio transport is not implemented on this platform"));
#endif
    }

    core::Result<std::string> read_line() {
        const auto started = ensure_started();
        if (!started) {
            return std::unexpected(started.error());
        }

#ifdef _WIN32
        std::string line;
        char ch = '\0';
        DWORD read = 0;
        while (true) {
            if (!ReadFile(stdout_read_.get(), &ch, 1, &read, nullptr) || read == 0) {
                return std::unexpected(make_process_error("failed to read process stdout",
                                                          windows_error_message(GetLastError())));
            }
            if (ch == '\n') {
                break;
            }
            if (ch != '\r') {
                line.push_back(ch);
            }
        }
        return line;
#else
        return std::unexpected(make_process_error("process stdio transport is not implemented on this platform"));
#endif
    }

private:
    core::Result<core::Unit> ensure_started() {
#ifdef _WIN32
        if (process_) {
            return core::Unit{};
        }
        if (options_.command.empty()) {
            return std::unexpected(make_process_error("process command must not be empty"));
        }

        SECURITY_ATTRIBUTES security;
        security.nLength = sizeof(security);
        security.lpSecurityDescriptor = nullptr;
        security.bInheritHandle = TRUE;

        HANDLE stdin_read_raw = nullptr;
        HANDLE stdin_write_raw = nullptr;
        if (!CreatePipe(&stdin_read_raw, &stdin_write_raw, &security, 0)) {
            return std::unexpected(make_process_error("failed to create process stdin pipe",
                                                      windows_error_message(GetLastError())));
        }
        Handle stdin_read(stdin_read_raw);
        stdin_write_.reset(stdin_write_raw);
        SetHandleInformation(stdin_write_.get(), HANDLE_FLAG_INHERIT, 0);

        HANDLE stdout_read_raw = nullptr;
        HANDLE stdout_write_raw = nullptr;
        if (!CreatePipe(&stdout_read_raw, &stdout_write_raw, &security, 0)) {
            return std::unexpected(make_process_error("failed to create process stdout pipe",
                                                      windows_error_message(GetLastError())));
        }
        stdout_read_.reset(stdout_read_raw);
        Handle stdout_write(stdout_write_raw);
        SetHandleInformation(stdout_read_.get(), HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW startup;
        ZeroMemory(&startup, sizeof(startup));
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = stdin_read.get();
        startup.hStdOutput = stdout_write.get();
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

        PROCESS_INFORMATION process_info;
        ZeroMemory(&process_info, sizeof(process_info));

        auto command_line = command_line_from_options(options_);
        auto environment = environment_block(options_);
        auto cwd = utf8_to_wide(options_.cwd);
        const BOOL created = CreateProcessW(nullptr,
                                            command_line.data(),
                                            nullptr,
                                            nullptr,
                                            TRUE,
                                            CREATE_NO_WINDOW | (environment.empty() ? 0 : CREATE_UNICODE_ENVIRONMENT),
                                            environment.empty() ? nullptr : environment.data(),
                                            cwd.empty() ? nullptr : cwd.c_str(),
                                            &startup,
                                            &process_info);
        if (!created) {
            return std::unexpected(make_process_error("failed to start process", windows_error_message(GetLastError())));
        }

        process_.reset(process_info.hProcess);
        thread_.reset(process_info.hThread);
        return core::Unit{};
#else
        return std::unexpected(make_process_error("process stdio transport is not implemented on this platform"));
#endif
    }

    void stop() noexcept {
#ifdef _WIN32
        stdin_write_.reset();
        if (process_) {
            const DWORD wait_result = WaitForSingleObject(process_.get(), 1000);
            if (wait_result == WAIT_TIMEOUT) {
                TerminateProcess(process_.get(), 1);
                WaitForSingleObject(process_.get(), 1000);
            }
        }
        stdout_read_.reset();
        thread_.reset();
        process_.reset();
#endif
    }

    ProcessStdioTransportOptions options_;

#ifdef _WIN32
    Handle stdin_write_;
    Handle stdout_read_;
    Handle process_;
    Handle thread_;
#endif
};

ProcessStdioTransport::ProcessStdioTransport(ProcessStdioTransportOptions options)
    : options_(std::move(options)),
      impl_(std::make_unique<Impl>(options_)) {}

ProcessStdioTransport::~ProcessStdioTransport() = default;

core::Result<protocol::JsonRpcResponse> ProcessStdioTransport::send(const protocol::JsonRpcRequest& request) {
    const auto serialized = protocol::serialize_request(request);
    if (!serialized) {
        return std::unexpected(serialized.error());
    }

    const auto written = impl_->write_line(*serialized);
    if (!written) {
        return std::unexpected(written.error());
    }

    const auto line = impl_->read_line();
    if (!line) {
        return std::unexpected(line.error());
    }

    return protocol::parse_response(*line);
}

core::Result<core::Unit> ProcessStdioTransport::send_notification(
        const protocol::JsonRpcNotification& notification) {
    const auto serialized = protocol::serialize_notification(notification);
    if (!serialized) {
        return std::unexpected(serialized.error());
    }

    return impl_->write_line(*serialized);
}

} // namespace mcp::client
