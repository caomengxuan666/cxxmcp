// Copyright (c) 2025 [caomengxuan666]

#include <cxxmcp/protocol/types.hpp>
#include <string>

int main() {
  mcp::protocol::Icon icon;
  icon.src = "icon.png";

  mcp::protocol::ProgressNotificationParams progress;
  progress.progress_token = std::string{"token"};
  progress.progress = 0.5;

  return icon.src.empty() ||
         !mcp::protocol::protocol_number_is_finite(progress.progress);
}
