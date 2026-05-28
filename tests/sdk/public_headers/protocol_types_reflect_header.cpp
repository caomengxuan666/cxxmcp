// Copyright (c) 2025 [caomengxuan666]

#include <cstdint>
#include <cxxmcp/protocol/types_reflect.hpp>
#include <string>

int main() {
  mcp::protocol::Icon icon;
  icon.src = "icon.png";
  const auto icon_json = mcp::protocol::icon_to_json(icon);
  const auto parsed_icon = mcp::protocol::icon_from_json(icon_json);
  if (!parsed_icon.has_value()) {
    return 10;
  }

  mcp::protocol::CancelledNotificationParams cancelled;
  cancelled.request_id = std::int64_t{1};
  const auto cancelled_json =
      mcp::protocol::cancelled_notification_params_to_json(cancelled);
  if (!mcp::protocol::cancelled_notification_params_from_json(cancelled_json)) {
    return 20;
  }

  mcp::protocol::ProgressNotificationParams progress;
  progress.progress_token = std::string{"token"};
  progress.progress = 0.5;
  const auto progress_json =
      mcp::protocol::progress_notification_params_to_json(progress);
  if (!mcp::protocol::progress_notification_params_from_json(progress_json)) {
    return 30;
  }

  return 0;
}
