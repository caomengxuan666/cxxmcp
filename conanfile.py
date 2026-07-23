from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy
import os


class CxxmcpConan(ConanFile):
    name = "cxxmcp"
    version = "1.2.4"  # overridden by set_version()
    package_type = "static-library"

    license = "MIT"
    url = "https://github.com/caomengxuan666/cxxmcp"
    homepage = "https://github.com/caomengxuan666/cxxmcp"
    description = "C++ MCP SDK for protocol, client, server, transport, peer, and handler APIs."
    topics = ("mcp", "model-context-protocol", "json-rpc", "sdk")

    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [False],
        "fPIC": [True, False],
        "with_http": [True, False],
        "with_websocket": [True, False],
        "with_auth": [True, False],
        "with_examples": [True, False],
        "with_tests": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "with_http": False,
        "with_websocket": False,
        "with_auth": False,
        "with_examples": False,
        "with_tests": False,
    }

    exports_sources = (
        "CMakeLists.txt",
        "CMakePresets.json",
        "VERSION",
        "cmake/*",
        "sdk/*",
        "examples/*",
        "tests/*",
        "third_party/tl/*",
        "third_party/nlohmann/*",
        "third_party/httplib/httplib.h",
        "README.md",
        "README_zh.md",
        "CHANGELOG.md",
        "LICENSE",
    )

    def set_version(self):
        version_file = os.path.join(self.recipe_folder, "VERSION")
        with open(version_file) as f:
            self.version = f.read().strip()

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        http_enabled = bool(self.options.with_http) or bool(
            self.options.with_websocket)

        toolchain = CMakeToolchain(self)
        toolchain.variables["CXXMCP_BUILD_SDK"] = True
        toolchain.variables["CXXMCP_BUILD_EXAMPLES"] = bool(
            self.options.with_examples)
        toolchain.variables["CXXMCP_BUILD_TESTS"] = bool(
            self.options.with_tests)
        toolchain.variables["CXXMCP_BUILD_DOCS"] = False
        toolchain.variables["CXXMCP_ENABLE_HTTP"] = http_enabled
        toolchain.variables["CXXMCP_ENABLE_WEBSOCKET"] = bool(
            self.options.with_websocket)
        toolchain.variables["CXXMCP_ENABLE_AUTH"] = bool(
            self.options.with_auth)
        toolchain.variables["BUILD_SHARED_LIBS"] = False
        if self.settings.os != "Windows":
            toolchain.variables["CMAKE_POSITION_INDEPENDENT_CODE"] = bool(
                self.options.fPIC)
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
        if self.options.with_tests:
            cmake.test(cli_args=["--output-on-failure", "--timeout", "600"])

    def package(self):
        cmake = CMake(self)
        cmake.install()
        copy(self, "LICENSE", src=self.source_folder,
             dst=os.path.join(self.package_folder, "licenses"))

    def package_info(self):
        http_enabled = bool(self.options.with_http) or bool(
            self.options.with_websocket)
        websocket_enabled = bool(self.options.with_websocket)
        optional_defines = []
        if http_enabled:
            optional_defines.append("CXXMCP_ENABLE_HTTP=1")
        if websocket_enabled:
            optional_defines.append("CXXMCP_ENABLE_WEBSOCKET=1")

        self.cpp_info.set_property("cmake_file_name", "cxxmcp")
        self.cpp_info.set_property("cmake_target_name", "cxxmcp::sdk")
        self.cpp_info.libs = ["mcp_server", "mcp_client", "mcp_protocol"]
        self.cpp_info.defines.extend(optional_defines)
        if self.settings.os == "Windows" and http_enabled:
            self.cpp_info.system_libs.extend(["ws2_32", "crypt32"])
        elif self.settings.os in ("Linux", "FreeBSD"):
            self.cpp_info.system_libs.append("pthread")

        self.cpp_info.components["core"].set_property(
            "cmake_target_name", "cxxmcp::core")

        self.cpp_info.components["protocol"].set_property(
            "cmake_target_name", "cxxmcp::protocol")
        self.cpp_info.components["protocol"].libs = ["mcp_protocol"]
        self.cpp_info.components["protocol"].requires = ["core"]

        self.cpp_info.components["transport"].set_property(
            "cmake_target_name", "cxxmcp::transport")
        self.cpp_info.components["transport"].requires = ["protocol"]

        self.cpp_info.components["client"].set_property(
            "cmake_target_name", "cxxmcp::client")
        self.cpp_info.components["client"].libs = ["mcp_client"]
        self.cpp_info.components["client"].requires = ["transport"]
        self.cpp_info.components["client"].defines.extend(optional_defines)
        if self.settings.os == "Windows" and http_enabled:
            self.cpp_info.components["client"].system_libs.extend(
                ["ws2_32", "crypt32"])
        elif self.settings.os in ("Linux", "FreeBSD"):
            self.cpp_info.components["client"].system_libs.append("pthread")

        self.cpp_info.components["server"].set_property(
            "cmake_target_name", "cxxmcp::server")
        self.cpp_info.components["server"].libs = ["mcp_server"]
        self.cpp_info.components["server"].requires = ["transport"]
        self.cpp_info.components["server"].defines.extend(optional_defines)
        if self.settings.os == "Windows" and http_enabled:
            self.cpp_info.components["server"].system_libs.extend(
                ["ws2_32", "crypt32"])
        elif self.settings.os in ("Linux", "FreeBSD"):
            self.cpp_info.components["server"].system_libs.append("pthread")

        self.cpp_info.components["peer"].set_property(
            "cmake_target_name", "cxxmcp::peer")
        self.cpp_info.components["peer"].requires = ["client", "server"]
        self.cpp_info.components["peer"].defines.extend(optional_defines)

        self.cpp_info.components["handler"].set_property(
            "cmake_target_name", "cxxmcp::handler")
        self.cpp_info.components["handler"].requires = ["client", "server"]
        self.cpp_info.components["handler"].defines.extend(optional_defines)

        self.cpp_info.components["service"].set_property(
            "cmake_target_name", "cxxmcp::service")
        self.cpp_info.components["service"].requires = ["peer"]
        self.cpp_info.components["service"].defines.extend(optional_defines)

        if self.options.with_auth:
            self.cpp_info.components["auth"].set_property(
                "cmake_target_name", "cxxmcp::auth")
            self.cpp_info.components["auth"].requires = ["core"]
