// Copyright (c) 2025 [caomengxuan666]

#include "cxxmcp/client/process_stdio_transport.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "cxxmcp/protocol/serialization.hpp"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#endif

#include "cxxmcp/transport/process_stdio_transport.hpp"

namespace mcp::client {
namespace {

core::Error make_process_error(int code, std::string message,
                               std::string detail = {}) {
  return core::Error{code, std::move(message), std::move(detail), "transport"};
}

core::Error make_process_error(std::string message, std::string detail = {}) {
  return make_process_error(
      static_cast<int>(protocol::ErrorCode::InternalError), std::move(message),
      std::move(detail));
}

std::string request_id_to_string(const protocol::RequestId& request_id) {
  return std::visit(
      [](const auto& value) -> std::string {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::string>) {
          return value;
        } else {
          return std::to_string(value);
        }
      },
      request_id);
}

#ifdef _WIN32

std::wstring utf8_to_wide(const std::string& value) {
  if (value.empty()) {
    return {};
  }
  const int size = MultiByteToWideChar(
      CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size);
  return result;
}

std::string windows_error_message(DWORD error) {
  LPWSTR buffer = nullptr;
  const DWORD size = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
  if (size == 0 || !buffer) {
    return "Windows error " + std::to_string(error);
  }

  std::wstring wide(buffer, size);
  LocalFree(buffer);
  const int utf8_size = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                            static_cast<int>(wide.size()),
                                            nullptr, 0, nullptr, nullptr);
  std::string result(static_cast<std::size_t>(utf8_size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                      result.data(), utf8_size, nullptr, nullptr);
  while (!result.empty() && (result.back() == '\r' || result.back() == '\n')) {
    result.pop_back();
  }
  return result;
}

std::wstring quote_arg(const std::wstring& arg) {
  if (arg.empty()) {
    return L"\"\"";
  }
  const bool needs_quotes =
      arg.find_first_of(L" \t\n\v\"") != std::wstring::npos;
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

std::wstring command_line_from_options(
    const ProcessStdioTransportOptions& options) {
  std::wstring command_line = quote_arg(utf8_to_wide(options.command));
  for (const auto& arg : options.args) {
    command_line.push_back(L' ');
    command_line += quote_arg(utf8_to_wide(arg));
  }
  return command_line;
}

std::vector<wchar_t> environment_block(
    const ProcessStdioTransportOptions& options) {
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
    const auto it =
        std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
          return _wcsnicmp(entry.c_str(), prefix.c_str(), prefix.size()) == 0;
        });
    if (it == entries.end()) {
      entries.push_back(prefix + utf8_to_wide(value));
    } else {
      *it = prefix + utf8_to_wide(value);
    }
  }

  std::sort(entries.begin(), entries.end(),
            [](const auto& lhs, const auto& rhs) {
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
  explicit Handle(HANDLE handle) : handle_(handle) {}
  ~Handle() { reset(); }

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

  HANDLE get() const noexcept { return handle_; }

  HANDLE release() noexcept { return std::exchange(handle_, nullptr); }

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

#else

std::string posix_error_message(int error) { return std::strerror(error); }

class ScopedSigpipeBlock final {
 public:
  ScopedSigpipeBlock() noexcept {
    ::sigemptyset(&sigpipe_set_);
    ::sigaddset(&sigpipe_set_, SIGPIPE);
    if (::pthread_sigmask(SIG_BLOCK, &sigpipe_set_, &previous_mask_) == 0) {
      blocked_ = true;
      sigset_t pending;
      if (::sigpending(&pending) == 0) {
        had_pending_sigpipe_ = ::sigismember(&pending, SIGPIPE) == 1;
      }
    }
  }

  ~ScopedSigpipeBlock() {
    if (!blocked_) {
      return;
    }

    if (!had_pending_sigpipe_) {
      sigset_t pending;
      if (::sigpending(&pending) == 0 &&
          ::sigismember(&pending, SIGPIPE) == 1) {
        int signal = 0;
        (void)::sigwait(&sigpipe_set_, &signal);
      }
    }

    (void)::pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr);
  }

  ScopedSigpipeBlock(const ScopedSigpipeBlock&) = delete;
  ScopedSigpipeBlock& operator=(const ScopedSigpipeBlock&) = delete;

 private:
  sigset_t sigpipe_set_{};
  sigset_t previous_mask_{};
  bool blocked_ = false;
  bool had_pending_sigpipe_ = false;
};

class FileDescriptor final {
 public:
  FileDescriptor() = default;
  explicit FileDescriptor(int fd) : fd_(fd) {}
  ~FileDescriptor() { reset(); }

  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;

  FileDescriptor(FileDescriptor&& other) noexcept
      : fd_(std::exchange(other.fd_, -1)) {}

  FileDescriptor& operator=(FileDescriptor&& other) noexcept {
    if (this != &other) {
      reset();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }

  int get() const noexcept { return fd_; }

  int release() noexcept { return std::exchange(fd_, -1); }

  void reset(int fd = -1) noexcept {
    if (fd_ >= 0) {
      while (::close(fd_) != 0 && errno == EINTR) {
      }
    }
    fd_ = fd;
  }

  explicit operator bool() const noexcept { return fd_ >= 0; }

 private:
  int fd_ = -1;
};

std::vector<char*> argv_from_options(
    const ProcessStdioTransportOptions& options) {
  std::vector<char*> argv;
  argv.reserve(options.args.size() + 2);
  argv.push_back(const_cast<char*>(options.command.c_str()));
  for (const auto& arg : options.args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  argv.push_back(nullptr);
  return argv;
}

void apply_child_environment(const ProcessStdioTransportOptions& options) {
  for (const auto& [key, value] : options.env) {
    if (!key.empty()) {
      ::setenv(key.c_str(), value.c_str(), 1);
    }
  }
}

bool wait_for_process_exit(pid_t pid,
                           std::chrono::milliseconds timeout) noexcept {
  int status = 0;
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    const pid_t waited = ::waitpid(pid, &status, WNOHANG);
    if (waited == pid || (waited < 0 && errno == ECHILD)) {
      return true;
    }
    if (waited < 0 && errno != EINTR) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

void reap_process(pid_t pid) noexcept {
  int status = 0;
  while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
}

#endif

}  // namespace

class ProcessStdioTransport::Impl {
 public:
  explicit Impl(ProcessStdioTransportOptions options)
      : options_(std::move(options)) {}

  ~Impl() { stop(); }

  core::Result<core::Unit> write_line(const std::string& line) {
    const auto started = ensure_started();
    if (!started) {
      return std::unexpected(started.error());
    }

#ifdef _WIN32
    std::lock_guard<std::mutex> lock(write_mutex_);
    const std::string payload = line + "\n";
    DWORD written = 0;
    if (!WriteFile(stdin_write_.get(), payload.data(),
                   static_cast<DWORD>(payload.size()), &written, nullptr) ||
        written != payload.size()) {
      return std::unexpected(
          make_process_error("failed to write process stdin",
                             windows_error_message(GetLastError())));
    }
    return core::Unit{};
#else
    std::lock_guard<std::mutex> lock(write_mutex_);
    const std::string payload = line + "\n";
    const ScopedSigpipeBlock sigpipe_block;
    std::size_t written = 0;
    while (written < payload.size()) {
      const auto result = ::write(stdin_write_.get(), payload.data() + written,
                                  payload.size() - written);
      if (result < 0) {
        if (errno == EINTR) {
          continue;
        }
        return std::unexpected(make_process_error(
            "failed to write process stdin", posix_error_message(errno)));
      }
      if (result == 0) {
        return std::unexpected(make_process_error(
            "failed to write process stdin", "write returned zero bytes"));
      }
      written += static_cast<std::size_t>(result);
    }
    return core::Unit{};
#endif
  }

  core::Result<core::Unit> start(
      TransportRequestHandler request_handler,
      TransportNotificationHandler notification_handler) {
    request_handler_ = std::move(request_handler);
    notification_handler_ = std::move(notification_handler);
    started_ = true;
    if (!reader_thread_.joinable()) {
      reader_thread_ = std::thread([this] { this->reader_loop(); });
    }
    return core::Unit{};
  }

  bool started() const noexcept { return started_; }

  const TransportRequestHandler& request_handler() const noexcept {
    return request_handler_;
  }

  const TransportNotificationHandler& notification_handler() const noexcept {
    return notification_handler_;
  }

  core::Result<protocol::JsonRpcResponse> send_request(
      const protocol::JsonRpcRequest& request) {
    const auto serialized = protocol::serialize_request(request);
    if (!serialized) {
      return std::unexpected(serialized.error());
    }

    std::promise<protocol::JsonRpcResponse> promise;
    auto future = promise.get_future();
    {
      std::lock_guard<std::mutex> lock(pending_mutex_);
      const auto [_, inserted] =
          pending_responses_.emplace(request.id, std::move(promise));
      if (!inserted) {
        return std::unexpected(make_process_error(
            static_cast<int>(protocol::ErrorCode::InvalidRequest),
            "duplicate process stdio request id",
            request_id_to_string(request.id)));
      }
    }

    const auto written = write_line(*serialized);
    if (!written) {
      std::lock_guard<std::mutex> lock(pending_mutex_);
      pending_responses_.erase(request.id);
      return std::unexpected(written.error());
    }

    if (!options_.request_timeout.has_value()) {
      return future.get();
    }

    if (future.wait_for(*options_.request_timeout) ==
        std::future_status::ready) {
      return future.get();
    }

    {
      std::lock_guard<std::mutex> lock(pending_mutex_);
      pending_responses_.erase(request.id);
    }

    return std::unexpected(make_process_error(
        static_cast<int>(protocol::ErrorCode::InternalError),
        "process stdio request timed out", request_id_to_string(request.id)));
  }

  void reader_loop() {
    while (running_) {
      const auto line = read_line();
      if (!line) {
        break;
      }
      if (!running_) {
        break;
      }

      const auto message = protocol::parse_message(*line);
      if (!message) {
        continue;
      }

      if (const auto* notification =
              std::get_if<protocol::JsonRpcNotification>(&*message)) {
        if (notification_handler_) {
          const auto handled = notification_handler_(*notification);
          if (!handled) {
            continue;
          }
        }
        continue;
      }

      if (const auto* incoming_request =
              std::get_if<protocol::JsonRpcRequest>(&*message)) {
        if (!request_handler_) {
          continue;
        }

        auto handled = request_handler_(*incoming_request);
        if (!handled) {
          handled = protocol::make_error_response(
              std::optional<protocol::RequestId>{incoming_request->id},
              protocol::make_error(
                  handled.error().code, handled.error().message,
                  handled.error().detail.empty()
                      ? std::nullopt
                      : std::optional<protocol::Json>{handled.error().detail}));
        }
        const auto serialized_response = protocol::serialize_response(*handled);
        if (!serialized_response) {
          continue;
        }
        const auto written = write_line(*serialized_response);
        if (!written) {
          continue;
        }
        continue;
      }

      const auto* response = std::get_if<protocol::JsonRpcResponse>(&*message);
      if (!response || !response->id.has_value()) {
        continue;
      }

      std::promise<protocol::JsonRpcResponse> promise;
      {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        auto it = pending_responses_.find(*response->id);
        if (it == pending_responses_.end()) {
          continue;
        }
        promise = std::move(it->second);
        pending_responses_.erase(it);
      }

      promise.set_value(*response);
    }

    fail_pending(make_process_error("process stdio transport reader stopped"));
  }

  void fail_pending(const core::Error& error) {
    std::map<protocol::RequestId, std::promise<protocol::JsonRpcResponse>>
        pending;
    {
      std::lock_guard<std::mutex> lock(pending_mutex_);
      pending.swap(pending_responses_);
    }

    for (auto& [id, promise] : pending) {
      promise.set_value(protocol::make_error_response(
          std::optional<protocol::RequestId>{id},
          protocol::make_error(
              error.code, error.message,
              error.detail.empty()
                  ? std::nullopt
                  : std::optional<protocol::Json>{error.detail})));
    }
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
        return std::unexpected(
            make_process_error("failed to read process stdout",
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
    std::string line;
    char ch = '\0';
    while (true) {
      const auto result = ::read(stdout_read_.get(), &ch, 1);
      if (result < 0) {
        if (errno == EINTR) {
          continue;
        }
        return std::unexpected(make_process_error(
            "failed to read process stdout", posix_error_message(errno)));
      }
      if (result == 0) {
        return std::unexpected(make_process_error(
            "failed to read process stdout", "child process closed stdout"));
      }
      if (ch == '\n') {
        break;
      }
      if (ch != '\r') {
        line.push_back(ch);
      }
    }
    return line;
#endif
  }

 private:
  core::Result<core::Unit> ensure_started() {
#ifdef _WIN32
    std::lock_guard<std::mutex> process_lock(process_mutex_);
    if (process_) {
      return core::Unit{};
    }
    if (options_.command.empty()) {
      return std::unexpected(
          make_process_error("process command must not be empty"));
    }

    SECURITY_ATTRIBUTES security;
    security.nLength = sizeof(security);
    security.lpSecurityDescriptor = nullptr;
    security.bInheritHandle = TRUE;

    HANDLE stdin_read_raw = nullptr;
    HANDLE stdin_write_raw = nullptr;
    if (!CreatePipe(&stdin_read_raw, &stdin_write_raw, &security, 0)) {
      return std::unexpected(
          make_process_error("failed to create process stdin pipe",
                             windows_error_message(GetLastError())));
    }
    Handle stdin_read(stdin_read_raw);
    stdin_write_.reset(stdin_write_raw);
    SetHandleInformation(stdin_write_.get(), HANDLE_FLAG_INHERIT, 0);

    HANDLE stdout_read_raw = nullptr;
    HANDLE stdout_write_raw = nullptr;
    if (!CreatePipe(&stdout_read_raw, &stdout_write_raw, &security, 0)) {
      return std::unexpected(
          make_process_error("failed to create process stdout pipe",
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
    const BOOL created = CreateProcessW(
        nullptr, command_line.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW |
            (environment.empty() ? 0 : CREATE_UNICODE_ENVIRONMENT),
        environment.empty() ? nullptr : environment.data(),
        cwd.empty() ? nullptr : cwd.c_str(), &startup, &process_info);
    if (!created) {
      return std::unexpected(make_process_error(
          "failed to start process", windows_error_message(GetLastError())));
    }

    process_.reset(process_info.hProcess);
    thread_.reset(process_info.hThread);
    return core::Unit{};
#else
    std::lock_guard<std::mutex> process_lock(process_mutex_);
    if (process_pid_ > 0) {
      return core::Unit{};
    }
    if (options_.command.empty()) {
      return std::unexpected(
          make_process_error("process command must not be empty"));
    }

    int stdin_pipe_raw[2] = {-1, -1};
    if (::pipe(stdin_pipe_raw) != 0) {
      return std::unexpected(make_process_error(
          "failed to create process stdin pipe", posix_error_message(errno)));
    }
    FileDescriptor stdin_read(stdin_pipe_raw[0]);
    FileDescriptor stdin_write(stdin_pipe_raw[1]);

    int stdout_pipe_raw[2] = {-1, -1};
    if (::pipe(stdout_pipe_raw) != 0) {
      return std::unexpected(make_process_error(
          "failed to create process stdout pipe", posix_error_message(errno)));
    }
    FileDescriptor stdout_read(stdout_pipe_raw[0]);
    FileDescriptor stdout_write(stdout_pipe_raw[1]);

    const pid_t pid = ::fork();
    if (pid < 0) {
      return std::unexpected(make_process_error("failed to start process",
                                                posix_error_message(errno)));
    }

    if (pid == 0) {
      stdin_write.reset();
      stdout_read.reset();

      if (::dup2(stdin_read.get(), STDIN_FILENO) < 0 ||
          ::dup2(stdout_write.get(), STDOUT_FILENO) < 0) {
        _exit(127);
      }
      stdin_read.reset();
      stdout_write.reset();

      if (!options_.cwd.empty() && ::chdir(options_.cwd.c_str()) != 0) {
        _exit(127);
      }
      apply_child_environment(options_);
      auto argv = argv_from_options(options_);
      ::execvp(options_.command.c_str(), argv.data());
      _exit(127);
    }

    stdin_read.reset();
    stdout_write.reset();
    stdin_write_ = std::move(stdin_write);
    stdout_read_ = std::move(stdout_read);
    process_pid_ = pid;
    return core::Unit{};
#endif
  }

 public:
  void stop() noexcept {
#ifdef _WIN32
    running_ = false;
    {
      std::lock_guard<std::mutex> process_lock(process_mutex_);
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
    }
    if (reader_thread_.joinable()) {
      reader_thread_.join();
    }
#else
    running_ = false;
    {
      std::lock_guard<std::mutex> process_lock(process_mutex_);
      stdin_write_.reset();
      if (process_pid_ > 0) {
        if (!wait_for_process_exit(process_pid_, std::chrono::seconds(1))) {
          ::kill(process_pid_, SIGTERM);
          if (!wait_for_process_exit(process_pid_, std::chrono::seconds(1))) {
            ::kill(process_pid_, SIGKILL);
            reap_process(process_pid_);
          }
        }
        process_pid_ = -1;
      }
      stdout_read_.reset();
    }
    if (reader_thread_.joinable() &&
        reader_thread_.get_id() != std::this_thread::get_id()) {
      reader_thread_.join();
    }
#endif
  }

 private:
  ProcessStdioTransportOptions options_;
  bool started_ = false;
  std::atomic_bool running_{true};
  TransportRequestHandler request_handler_;
  TransportNotificationHandler notification_handler_;
  std::thread reader_thread_;
  std::mutex write_mutex_;
  std::mutex pending_mutex_;
  std::mutex process_mutex_;
  std::map<protocol::RequestId, std::promise<protocol::JsonRpcResponse>>
      pending_responses_;

#ifdef _WIN32
  Handle stdin_write_;
  Handle stdout_read_;
  Handle process_;
  Handle thread_;
#else
  FileDescriptor stdin_write_;
  FileDescriptor stdout_read_;
  pid_t process_pid_ = -1;
#endif
};

ProcessStdioTransport::ProcessStdioTransport(
    ProcessStdioTransportOptions options)
    : options_(std::move(options)), impl_(std::make_unique<Impl>(options_)) {}

ProcessStdioTransport::~ProcessStdioTransport() = default;

core::Result<core::Unit> ProcessStdioTransport::start(
    TransportRequestHandler request_handler,
    TransportNotificationHandler notification_handler) {
  return impl_->start(std::move(request_handler),
                      std::move(notification_handler));
}

core::Result<protocol::JsonRpcResponse> ProcessStdioTransport::send(
    const protocol::JsonRpcRequest& request) {
  if (!impl_->started()) {
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

  return impl_->send_request(request);
}

core::Result<core::Unit> ProcessStdioTransport::send_notification(
    const protocol::JsonRpcNotification& notification) {
  const auto serialized = protocol::serialize_notification(notification);
  if (!serialized) {
    return std::unexpected(serialized.error());
  }

  return impl_->write_line(*serialized);
}

void ProcessStdioTransport::stop() noexcept { impl_->stop(); }

}  // namespace mcp::client

namespace mcp::transport {
namespace {

core::Error make_native_process_error(protocol::ErrorCode code,
                                      std::string message,
                                      std::string detail = {}) {
  return core::Error{
      static_cast<int>(code),
      std::move(message),
      std::move(detail),
      "transport",
  };
}

client::ProcessStdioTransportOptions to_legacy_options(
    ProcessStdioClientTransportOptions options) {
  client::ProcessStdioTransportOptions legacy;
  legacy.command = std::move(options.command);
  legacy.args = std::move(options.args);
  legacy.cwd = std::move(options.cwd);
  legacy.env = std::move(options.env);
  legacy.request_timeout = options.request_timeout;
  return legacy;
}

std::string request_id_to_string_for_native(
    const protocol::RequestId& request_id) {
  return std::visit(
      [](const auto& value) -> std::string {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::string>) {
          return value;
        } else {
          return std::to_string(value);
        }
      },
      request_id);
}

}  // namespace

class ProcessStdioClientTransport::Impl {
 public:
  explicit Impl(ProcessStdioClientTransportOptions options)
      : transport_(to_legacy_options(std::move(options))) {}

  ~Impl() { (void)close(); }

  protocol::Json diagnostics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return protocol::Json{
        {"name", "process-stdio"},
        {"closed", closed_},
        {"queued", inbound_.size()},
        {"pendingServerRequests", pending_server_requests_.size()},
        {"requestWorkers", request_threads_.size()},
        {"activeRequestWorkers", active_request_workers_},
        {"completedRequestWorkers", completed_request_workers_},
        {"failedRequestWorkers", failed_request_workers_},
        {"timedOutRequestWorkers", timed_out_request_workers_},
    };
  }

  core::Result<core::Unit> send(protocol::JsonRpcMessage message) {
    const auto started = ensure_started();
    if (!started) {
      return std::unexpected(started.error());
    }

    if (auto* request = std::get_if<protocol::JsonRpcRequest>(&message)) {
      return start_request_thread(std::move(*request));
    }

    if (auto* notification =
            std::get_if<protocol::JsonRpcNotification>(&message)) {
      return transport_.send_notification(*notification);
    }

    auto* response = std::get_if<protocol::JsonRpcResponse>(&message);
    if (response == nullptr || !response->id.has_value()) {
      return std::unexpected(make_native_process_error(
          protocol::ErrorCode::InvalidRequest,
          "process stdio client transport cannot send response without id"));
    }
    return complete_server_request(std::move(*response));
  }

  core::Result<std::optional<protocol::JsonRpcMessage>> receive() {
    const auto started = ensure_started();
    if (!started) {
      return std::unexpected(started.error());
    }

    std::unique_lock<std::mutex> lock(mutex_);
    receive_cv_.wait(lock, [this] { return closed_ || !inbound_.empty(); });
    if (inbound_.empty()) {
      return std::nullopt;
    }
    auto message = std::move(inbound_.front());
    inbound_.pop_front();
    return message;
  }

  core::Result<core::Unit> close() {
    std::map<protocol::RequestId, std::shared_ptr<PendingServerRequest>>
        pending;
    std::vector<std::thread> request_threads;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return core::Unit{};
      }
      closed_ = true;
      pending.swap(pending_server_requests_);
      request_threads.swap(request_threads_);
    }

    for (auto& [_, request] : pending) {
      {
        std::lock_guard<std::mutex> request_lock(request->mutex);
        request->response = protocol::make_error_response(
            std::optional<protocol::RequestId>{request->id},
            protocol::make_error(protocol::ErrorCode::InternalError,
                                 "process stdio transport closed"));
      }
      request->cv.notify_all();
    }

    receive_cv_.notify_all();
    transport_.stop();
    for (auto& thread : request_threads) {
      if (thread.joinable() && thread.get_id() != std::this_thread::get_id()) {
        thread.join();
      }
    }
    return core::Unit{};
  }

 private:
  struct PendingServerRequest {
    explicit PendingServerRequest(protocol::RequestId request_id)
        : id(std::move(request_id)) {}

    protocol::RequestId id;
    std::mutex mutex;
    std::condition_variable cv;
    std::optional<protocol::JsonRpcResponse> response;
  };

  core::Result<core::Unit> ensure_started() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return std::unexpected(make_native_process_error(
          protocol::ErrorCode::InvalidRequest,
          "process stdio client transport is closed"));
    }
    if (started_) {
      return core::Unit{};
    }
    started_ = true;

    auto started = transport_.start(
        [this](const protocol::JsonRpcRequest& request) {
          return handle_server_request(request);
        },
        [this](const protocol::JsonRpcNotification& notification) {
          return handle_server_notification(notification);
        });
    if (!started) {
      started_ = false;
      return std::unexpected(started.error());
    }
    return core::Unit{};
  }

  core::Result<core::Unit> start_request_thread(
      protocol::JsonRpcRequest request) {
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return std::unexpected(make_native_process_error(
            protocol::ErrorCode::InvalidRequest,
            "process stdio client transport is closed"));
      }
      ++active_request_workers_;
      request_threads_.emplace_back(
          [this, request = std::move(request)]() mutable {
            auto response = transport_.send(request);
            if (response) {
              finish_request_worker(false, false);
              enqueue(protocol::JsonRpcMessage{std::move(*response)});
              return;
            }
            finish_request_worker(true, is_timeout_error(response.error()));
            enqueue(protocol::JsonRpcMessage{protocol::make_error_response(
                std::optional<protocol::RequestId>{request.id},
                protocol::make_error(response.error().code,
                                     response.error().message,
                                     response.error().detail.empty()
                                         ? std::nullopt
                                         : std::optional<protocol::Json>{
                                               response.error().detail}))});
          });
    } catch (const std::system_error& ex) {
      finish_request_worker(true, false);
      return std::unexpected(make_native_process_error(
          protocol::ErrorCode::InternalError,
          "failed to start process stdio request worker", ex.what()));
    }
    return core::Unit{};
  }

  core::Result<protocol::JsonRpcResponse> handle_server_request(
      const protocol::JsonRpcRequest& request) {
    auto pending = std::make_shared<PendingServerRequest>(request.id);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return protocol::make_error_response(
            std::optional<protocol::RequestId>{request.id},
            protocol::make_error(protocol::ErrorCode::InternalError,
                                 "process stdio transport closed"));
      }
      const auto [_, inserted] =
          pending_server_requests_.emplace(request.id, pending);
      if (!inserted) {
        return protocol::make_error_response(
            std::optional<protocol::RequestId>{request.id},
            protocol::make_error(protocol::ErrorCode::InvalidRequest,
                                 "duplicate server request id"));
      }
      inbound_.push_back(protocol::JsonRpcMessage{request});
    }

    receive_cv_.notify_one();

    std::unique_lock<std::mutex> pending_lock(pending->mutex);
    pending->cv.wait(pending_lock,
                     [&pending] { return pending->response.has_value(); });
    return std::move(*pending->response);
  }

  core::Result<core::Unit> handle_server_notification(
      const protocol::JsonRpcNotification& notification) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return core::Unit{};
      }
      inbound_.push_back(protocol::JsonRpcMessage{notification});
    }
    receive_cv_.notify_one();
    return core::Unit{};
  }

  void enqueue(protocol::JsonRpcMessage message) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return;
      }
      inbound_.push_back(std::move(message));
    }
    receive_cv_.notify_one();
  }

  static bool is_timeout_error(const core::Error& error) {
    return error.message.find("timed out") != std::string::npos;
  }

  void finish_request_worker(bool failed, bool timed_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_request_workers_ > 0) {
      --active_request_workers_;
    }
    ++completed_request_workers_;
    if (failed) {
      ++failed_request_workers_;
    }
    if (timed_out) {
      ++timed_out_request_workers_;
    }
  }

  core::Result<core::Unit> complete_server_request(
      protocol::JsonRpcResponse response) {
    const auto id = *response.id;
    std::shared_ptr<PendingServerRequest> pending;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto it = pending_server_requests_.find(id);
      if (it == pending_server_requests_.end()) {
        return std::unexpected(make_native_process_error(
            protocol::ErrorCode::InvalidRequest,
            "process stdio client transport has no pending server request",
            request_id_to_string_for_native(id)));
      }
      pending = it->second;
      pending_server_requests_.erase(it);
    }

    {
      std::lock_guard<std::mutex> pending_lock(pending->mutex);
      pending->response = std::move(response);
    }
    pending->cv.notify_all();
    return core::Unit{};
  }

  client::ProcessStdioTransport transport_;
  mutable std::mutex mutex_;
  std::condition_variable receive_cv_;
  std::deque<protocol::JsonRpcMessage> inbound_;
  std::map<protocol::RequestId, std::shared_ptr<PendingServerRequest>>
      pending_server_requests_;
  std::vector<std::thread> request_threads_;
  std::size_t active_request_workers_ = 0;
  std::size_t completed_request_workers_ = 0;
  std::size_t failed_request_workers_ = 0;
  std::size_t timed_out_request_workers_ = 0;
  bool started_ = false;
  bool closed_ = false;
};

ProcessStdioClientTransport::ProcessStdioClientTransport(
    ProcessStdioClientTransportOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

ProcessStdioClientTransport::~ProcessStdioClientTransport() = default;

std::string_view ProcessStdioClientTransport::name() const noexcept {
  return "process-stdio";
}

protocol::Json ProcessStdioClientTransport::diagnostics() const {
  return impl_->diagnostics();
}

core::Result<core::Unit> ProcessStdioClientTransport::send(TxMessage message) {
  return impl_->send(std::move(message));
}

core::Result<std::optional<ProcessStdioClientTransport::RxMessage>>
ProcessStdioClientTransport::receive() {
  return impl_->receive();
}

core::Result<core::Unit> ProcessStdioClientTransport::close() {
  return impl_->close();
}

}  // namespace mcp::transport
