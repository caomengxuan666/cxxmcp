package("cxxmcp")
    set_homepage("https://github.com/caomengxuan666/cxxmcp")
    set_description("C++ MCP SDK for protocol, client, server, transport, peer, and handler APIs.")
    set_license("MIT")

    add_urls("https://github.com/caomengxuan666/cxxmcp/releases/download/$(version)/cxxmcp-sdk-source-$(version).tar.gz")
    add_versions("v2.0.2", "3c4ad678a8612183a4f2539973328b6a85dab360991a86e6328ca032cc5e2ba8")

    add_deps("cmake")

    on_install(function (package)
        local configs = {
            "-DCXXMCP_BUILD_SDK=ON",
            "-DCXXMCP_BUILD_RUNTIME=OFF",
            "-DCXXMCP_BUILD_APP=OFF",
            "-DCXXMCP_BUILD_GATEWAY=OFF",
            "-DCXXMCP_BUILD_CLI=OFF",
            "-DCXXMCP_BUILD_EXAMPLES=OFF",
            "-DCXXMCP_BUILD_TESTS=OFF",
            "-DCXXMCP_BUILD_DOCS=OFF",
            "-DBUILD_SHARED_LIBS=OFF"
        }
        import("package.tools.cmake").install(package, configs)
    end)

    on_test(function (package)
        assert(package:has_cxxincludes("cxxmcp/sdk.hpp", {configs = {languages = "c++17"}}))
    end)
