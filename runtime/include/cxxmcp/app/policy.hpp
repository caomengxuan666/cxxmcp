#pragma once

#include <string>
#include <unordered_set>

namespace mcp::app {

    enum class Permission {
        network_access,
        filesystem_read,
        filesystem_write,
        command_execution,
    };

    enum class ApprovalState {
        pending,
        approved,
        denied,
    };

    struct Policy {
        ApprovalState approval = ApprovalState::pending;
        std::unordered_set<Permission> permissions;
        bool enabled = false;
    };

}// namespace mcp::app
