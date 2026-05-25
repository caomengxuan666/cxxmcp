// Copyright (c) 2025 [caomengxuan666]

#pragma once

/// @file
/// @brief Stable public error helpers for SDK request and dispatch paths.
///
/// Public cxxmcp APIs return core::Result<T> instead of throwing. These helpers
/// keep common protocol, transport, timeout, cancellation, and handler failures
/// on stable JSON-RPC-compatible codes and message strings.

#include <chrono>
#include <string>
#include <utility>

#include "cxxmcp/core/result.hpp"
#include "cxxmcp/protocol/types.hpp"

namespace mcp::errors {

inline core::Error make(protocol::ErrorCode code, std::string message,
                        std::string detail = {}) {
  return core::Error{static_cast<int>(code), std::move(message),
                     std::move(detail)};
}

inline core::Error parse(std::string detail = {}) {
  return make(protocol::ErrorCode::ParseError, "parse error",
              std::move(detail));
}

inline core::Error invalid_request(std::string detail = {}) {
  return make(protocol::ErrorCode::InvalidRequest, "invalid request",
              std::move(detail));
}

inline core::Error invalid_params(std::string detail = {}) {
  return make(protocol::ErrorCode::InvalidParams, "invalid params",
              std::move(detail));
}

inline core::Error method_not_found(std::string detail = {}) {
  return make(protocol::ErrorCode::MethodNotFound, "method not found",
              std::move(detail));
}

inline core::Error handler_failed(std::string detail = {}) {
  return make(protocol::ErrorCode::InternalError, "handler failed",
              std::move(detail));
}

inline core::Error transport_failed(std::string detail = {}) {
  return make(protocol::ErrorCode::InternalError, "transport failed",
              std::move(detail));
}

inline core::Error request_cancelled() {
  return make(protocol::ErrorCode::InternalError, "request cancelled");
}

inline core::Error request_timed_out(std::chrono::milliseconds timeout) {
  return make(protocol::ErrorCode::InternalError, "request timed out",
              std::to_string(timeout.count()) + "ms");
}

inline core::Error request_task_missing() {
  return make(protocol::ErrorCode::InternalError,
              "request task is not configured");
}

inline core::Error request_state_missing() {
  return make(protocol::ErrorCode::InternalError,
              "request handle has no response state");
}

inline core::Error request_worker_exception(std::string detail = {}) {
  return make(protocol::ErrorCode::InternalError,
              "request worker threw an exception", std::move(detail));
}

inline core::Error request_worker_unknown_exception() {
  return make(protocol::ErrorCode::InternalError,
              "request worker threw an unknown exception");
}

inline protocol::ErrorObject to_json_rpc_error(const core::Error& error) {
  protocol::ErrorObject object;
  object.code = error.code;
  object.message = error.message;
  if (!error.detail.empty()) {
    object.data = error.detail;
  }
  return object;
}

}  // namespace mcp::errors
