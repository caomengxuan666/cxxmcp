#include "mcp/gui/app.hpp"

#include <iostream>
#include <string_view>

namespace {

void write_usage(std::ostream& out) {
    out << "Usage:\n"
        << "  mcp-gui [--minimized]\n";
}

} // namespace

int main(int argc, char** argv) {
    mcp::gui::GuiOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--minimized") {
            options.start_minimized = true;
            continue;
        }
        if (arg == "--help" || arg == "-h") {
            write_usage(std::cout);
            return 0;
        }

        std::cerr << "Unknown option: " << arg << '\n';
        write_usage(std::cerr);
        return 2;
    }

    const auto result = mcp::gui::run_gui(options);
    if (!result) {
        std::cerr << result.error().message;
        if (!result.error().detail.empty()) {
            std::cerr << ": " << result.error().detail;
        }
        std::cerr << '\n';
        return result.error().code;
    }

    return *result;
}
