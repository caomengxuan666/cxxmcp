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
                        std::string detail = {}, std::string category = {}) {
  return core::Error{static_cast<int>(code), std::move(message),
                     std::move(detail), std::move(category)};
}

inline core::Error parse(std::string detail = {}) {
  return make(protocol::ErrorCode::ParseError, "parse error", std::move(detail),
              "protocol");
}

inline core::Error invalid_request(std::string detail = {}) {
  return make(protocol::ErrorCode::InvalidRequest, "invalid request",
              std::move(detail), "protocol");
}

inline core::Error invalid_params(std::string detail = {}) {
  return make(protocol::ErrorCode::InvalidParams, "invalid params",
              std::move(detail), "protocol");
}

inline core::Error method_not_found(std::string detail = {}) {
  return make(protocol::ErrorCode::MethodNotFound, "method not found",
              std::move(detail), "protocol");
}

inline core::Error tool_not_found(std::string detail = {}) {
  return make(protocol::ErrorCode::ToolNotFound, "tool not found",
              std::move(detail), "tool");
}

inline core::Error resource_not_found(std::string detail = {}) {
  return make(protocol::ErrorCode::ResourceNotFound, "resource not found",
              std::move(detail), "resource");
}

inline core::Error permission_denied(std::string detail = {}) {
  return make(protocol::ErrorCode::PermissionDenied, "permission denied",
              std::move(detail), "permission");
}

inline core::Error rate_limited(std::string detail = {}) {
  return make(protocol::ErrorCode::RateLimited, "rate limited",
              std::move(detail), "rate_limit");
}

inline core::Error url_elicitation_required(std::string detail = {}) {
  return make(protocol::ErrorCode::UrlElicitationRequired,
              "url elicitation required", std::move(detail), "elicitation");
}

inline core::Error handler_failed(std::string detail = {}) {
  return make(protocol::ErrorCode::InternalError, "handler failed",
              std::move(detail), "handler");
}

inline core::Error handler_unknown_exception() {
  return make(protocol::ErrorCode::InternalError,
              "handler threw an unknown exception", {}, "handler");
}

inline core::Error transport_failed(std::string detail = {}) {
  return make(protocol::ErrorCode::InternalError, "transport failed",
              std::move(detail), "transport");
}

inline core::Error transport_closed(std::string detail = {}) {
  return make(protocol::ErrorCode::InternalError, "transport closed",
              std::move(detail), "transport");
}

inline core::Error transport_unexpected_response(std::string detail = {}) {
  return make(protocol::ErrorCode::InternalError, "unexpected response",
              std::move(detail), "transport");
}

inline core::Error transport_duplicate_request(std::string detail = {}) {
  return make(protocol::ErrorCode::InternalError, "duplicate request id",
              std::move(detail), "transport");
}

inline core::Error request_cancelled() {
  return make(protocol::ErrorCode::InternalError, "request cancelled", {},
              "cancellation");
}

inline core::Error request_timed_out(std::chrono::milliseconds timeout) {
  return make(protocol::ErrorCode::InternalError, "request timed out",
              std::to_string(timeout.count()) + "ms", "timeout");
}

inline core::Error request_task_missing() {
  return make(protocol::ErrorCode::InternalError,
              "request task is not configured", {}, "request");
}

inline core::Error request_state_missing() {
  return make(protocol::ErrorCode::InternalError,
              "request handle has no response state", {}, "request");
}

inline core::Error request_worker_exception(std::string detail = {}) {
  return make(protocol::ErrorCode::InternalError,
              "request worker threw an exception", std::move(detail),
              "request");
}

inline core::Error request_worker_unknown_exception() {
  return make(protocol::ErrorCode::InternalError,
              "request worker threw an unknown exception", {}, "request");
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
