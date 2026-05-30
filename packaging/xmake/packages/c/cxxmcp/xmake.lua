package("cxxmcp")
    set_homepage("https://caomengxuan666.github.io/cxxmcp")
    set_description("C++ MCP SDK for protocol, client, server, transport, peer, and handler APIs.")
    set_license("MIT")

    add_urls("https://github.com/caomengxuan666/cxxmcp/releases/download/$(version)/cxxmcp-sdk-source-$(version).tar.gz")
    add_versions("v1.1.3", "ad4edc8333c481e3ccbe5d4fb9cfddaca664ceb68bce169e25c0f07397e6dfb4")
    add_configs("http", {description = "Build HTTP/SSE transport (requires cpp-httplib).", default = false, type = "boolean"})
    add_configs("auth", {description = "Build the optional OAuth 2.1 / DPoP auth scaffold target.", default = false, type = "boolean"})

    add_deps("cmake")

    on_load(function (package)
        package:add("links", "mcp_server", "mcp_client", "mcp_protocol")
        if package:is_plat("windows") and package:config("http") then
            package:add("syslinks", "ws2_32")
        elseif package:is_plat("linux", "bsd") then
            package:add("syslinks", "pthread")
        end
    end)

    on_install(function (package)
        if package:config("shared") then
            raise("cxxmcp currently supports static library linkage only")
        end
        local configs = {
            "-DCXXMCP_BUILD_SDK=ON",
            "-DCXXMCP_BUILD_EXAMPLES=OFF",
            "-DCXXMCP_BUILD_TESTS=OFF",
            "-DCXXMCP_BUILD_DOCS=OFF",
            "-DCXXMCP_ENABLE_HTTP=" .. (package:config("http") and "ON" or "OFF"),
            "-DCXXMCP_ENABLE_AUTH=" .. (package:config("auth") and "ON" or "OFF"),
            "-DCXXMCP_USE_SYSTEM_DEPS=OFF",
            "-DBUILD_SHARED_LIBS=OFF"
        }
        import("package.tools.cmake").install(package, configs)
    end)

    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            #include <cstdint>
            #include <cxxmcp/protocol/serialization.hpp>
            void test() {
                auto request = mcp::protocol::make_ping_request(std::int64_t{1});
                auto serialized = mcp::protocol::serialize_request(request);
                (void)serialized;
            }
        ]]}, {configs = {languages = "c++17"}}))
        if package:config("auth") then
            assert(package:check_cxxsnippets({test = [[
                #include <cxxmcp/auth.hpp>
                void test() {
                    mcp::auth::ClientRegistrationOptions options;
                    options.redirect_uri = "http://localhost/callback";
                    auto request = mcp::auth::build_client_registration_request(options);
                    (void)request;
                }
            ]]}, {configs = {languages = "c++17"}}))
        end
    end)
