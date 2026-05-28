// Copyright (c) 2025 [caomengxuan666]

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cxxmcp/protocol/serialization.hpp"

namespace {

// Allocation tracking is disabled under sanitizers because they provide
// their own operator new/delete and the multiple-definition link is not
// resolvable.
#if defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer) || \
    __has_feature(memory_sanitizer)
#define CXXMCP_BENCH_TRACK_ALLOCATIONS 0
#else
#define CXXMCP_BENCH_TRACK_ALLOCATIONS 1
#endif
#elif !defined(__SANITIZE_THREAD__) && !defined(__SANITIZE_ADDRESS__) && \
    !defined(__SANITIZE_MEMORY__)
#define CXXMCP_BENCH_TRACK_ALLOCATIONS 1
#else
#define CXXMCP_BENCH_TRACK_ALLOCATIONS 0
#endif

std::atomic_bool g_count_allocations{false};
std::atomic<std::size_t> g_allocation_count{0};
std::atomic<std::size_t> g_allocation_bytes{0};

void record_allocation(std::size_t size) noexcept {
#if CXXMCP_BENCH_TRACK_ALLOCATIONS
  if (g_count_allocations.load(std::memory_order_relaxed)) {
    g_allocation_count.fetch_add(1, std::memory_order_relaxed);
    g_allocation_bytes.fetch_add(size, std::memory_order_relaxed);
  }
#else
  (void)size;
#endif
}

void reset_allocation_counters() noexcept {
  g_allocation_count.store(0, std::memory_order_relaxed);
  g_allocation_bytes.store(0, std::memory_order_relaxed);
}

struct AllocationScope {
  AllocationScope() {
    reset_allocation_counters();
    g_count_allocations.store(true, std::memory_order_relaxed);
  }

  ~AllocationScope() {
    g_count_allocations.store(false, std::memory_order_relaxed);
  }
};

using Clock = std::chrono::steady_clock;
using Json = mcp::protocol::Json;

struct BenchmarkResult {
  std::string name;
  std::size_t iterations = 0;
  std::size_t output_bytes = 0;
  std::size_t allocations = 0;
  std::size_t allocated_bytes = 0;
  std::chrono::nanoseconds elapsed{0};
};

Json large_payload() {
  Json payload = Json::object();
  payload["query"] = std::string(4096, 'q');
  payload["items"] = Json::array();
  for (int index = 0; index < 64; ++index) {
    payload["items"].push_back(
        Json{{"id", index}, {"name", "item-" + std::to_string(index)}});
  }
  return payload;
}

template <class Callable>
BenchmarkResult run_benchmark(std::string name, std::size_t iterations,
                              Callable callable) {
  BenchmarkResult result;
  result.name = std::move(name);
  result.iterations = iterations;

  const auto started = Clock::now();
  {
    AllocationScope allocation_scope;
    for (std::size_t index = 0; index < iterations; ++index) {
      auto serialized = callable();
      if (!serialized.has_value()) {
        throw std::runtime_error("serialization benchmark failed: " +
                                 serialized.error().message);
      }
      result.output_bytes += serialized->size();
    }
  }
  result.elapsed = Clock::now() - started;
  result.allocations = g_allocation_count.load(std::memory_order_relaxed);
  result.allocated_bytes = g_allocation_bytes.load(std::memory_order_relaxed);
  return result;
}

void print_result(const BenchmarkResult& result) {
  const auto micros =
      std::chrono::duration_cast<std::chrono::microseconds>(result.elapsed)
          .count();
  std::cout << result.name << ": iterations=" << result.iterations
            << " output_bytes=" << result.output_bytes
            << " elapsed_us=" << micros << " allocations=" << result.allocations
            << " allocated_bytes=" << result.allocated_bytes << '\n';
}

}  // namespace

#if CXXMCP_BENCH_TRACK_ALLOCATIONS

void* operator new(std::size_t size) {
  record_allocation(size);
  if (void* pointer = std::malloc(size)) {
    return pointer;
  }
  throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
  record_allocation(size);
  if (void* pointer = std::malloc(size)) {
    return pointer;
  }
  throw std::bad_alloc();
}

void operator delete(void* pointer) noexcept { std::free(pointer); }

void operator delete[](void* pointer) noexcept { std::free(pointer); }

void operator delete(void* pointer, std::size_t) noexcept {
  std::free(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
  std::free(pointer);
}

#endif  // CXXMCP_BENCH_TRACK_ALLOCATIONS

int main() {
  constexpr std::size_t kIterations = 1000;

  const Json large = large_payload();
  const Json meta = Json{{"traceId", "bench"}, {"progressToken", 42}};

  mcp::protocol::JsonRpcRequest small_request;
  small_request.method = std::string(mcp::protocol::PingMethod);
  small_request.id = std::int64_t{1};

  mcp::protocol::JsonRpcRequest large_request = small_request;
  large_request.params = large;

  mcp::protocol::JsonRpcRequest large_request_with_meta = large_request;
  large_request_with_meta.meta = meta;

  mcp::protocol::JsonRpcNotification notification;
  notification.method = std::string(mcp::protocol::InitializedMethod);
  notification.params = large;
  notification.meta = meta;

  mcp::protocol::JsonRpcResponse response =
      mcp::protocol::make_response(std::int64_t{1}, large);
  response.meta = meta;

  mcp::protocol::JsonRpcResponse error_response =
      mcp::protocol::make_error_response(
          std::int64_t{1}, mcp::protocol::make_error(
                               mcp::protocol::ErrorCode::InternalError,
                               "benchmark error", std::optional<Json>{large}));

  std::vector<BenchmarkResult> results;
  results.push_back(run_benchmark("request.small.no_meta", kIterations, [&] {
    return mcp::protocol::serialize_request(small_request);
  }));
  results.push_back(run_benchmark("request.large.no_meta", kIterations, [&] {
    return mcp::protocol::serialize_request(large_request);
  }));
  results.push_back(run_benchmark("request.large.with_meta", kIterations, [&] {
    return mcp::protocol::serialize_request(large_request_with_meta);
  }));
  results.push_back(run_benchmark(
      "notification.large.with_meta", kIterations,
      [&] { return mcp::protocol::serialize_notification(notification); }));
  results.push_back(run_benchmark("response.large.with_meta", kIterations, [&] {
    return mcp::protocol::serialize_response(response);
  }));
  results.push_back(run_benchmark(
      "response.error.large_data", kIterations,
      [&] { return mcp::protocol::serialize_response(error_response); }));

  for (const auto& result : results) {
    print_result(result);
  }

  return 0;
}
