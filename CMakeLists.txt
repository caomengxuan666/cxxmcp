cmake_minimum_required(VERSION 3.23)

project(cxxmcp
    VERSION 2.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries by default" FORCE)

include(CTest)

option(MCP_BUILD_PROTOCOL "Build the MCP protocol library" ON)
option(MCP_BUILD_CLIENT "Build the MCP client library" OFF)
option(MCP_BUILD_SERVER "Build the MCP server library" OFF)
option(MCP_BUILD_APP "Build the application service library" OFF)
option(MCP_BUILD_GATEWAY "Build the gateway service library" OFF)
option(MCP_BUILD_CLI "Build the command-line application" OFF)
option(MCP_BUILD_EXAMPLES "Build example executables" OFF)
option(MCP_BUILD_TESTS "Build tests for enabled layers" ${BUILD_TESTING})

unset(MCP_BUILD_GUI CACHE)

set(MCP_ENABLE_PROTOCOL ${MCP_BUILD_PROTOCOL})
set(MCP_ENABLE_CLIENT ${MCP_BUILD_CLIENT})
set(MCP_ENABLE_SERVER ${MCP_BUILD_SERVER})
set(MCP_ENABLE_APP ${MCP_BUILD_APP})
set(MCP_ENABLE_GATEWAY ${MCP_BUILD_GATEWAY})
set(MCP_ENABLE_CLI ${MCP_BUILD_CLI})

if(MCP_BUILD_CLI)
    set(MCP_ENABLE_GATEWAY ON)
endif()

if(MCP_ENABLE_GATEWAY)
    set(MCP_ENABLE_APP ON)
    set(MCP_ENABLE_SERVER ON)
endif()

if(MCP_ENABLE_APP)
    set(MCP_ENABLE_CLIENT ON)
endif()

if(MCP_ENABLE_CLIENT OR MCP_ENABLE_SERVER OR MCP_ENABLE_APP OR MCP_ENABLE_GATEWAY OR MCP_ENABLE_CLI)
    set(MCP_ENABLE_PROTOCOL ON)
endif()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING
        "Use the static MSVC runtime" FORCE)
endif()

add_library(mcp_core INTERFACE)
target_include_directories(mcp_core INTERFACE core/include)

add_library(jsonrpcpp INTERFACE)
target_include_directories(jsonrpcpp INTERFACE
    third_party/jsonrpcpp
    third_party
)

add_library(cpp_httplib INTERFACE)
target_include_directories(cpp_httplib INTERFACE
    third_party/httplib
)

add_subdirectory(third_party/spdlog EXCLUDE_FROM_ALL)

if(MCP_ENABLE_PROTOCOL)
    add_library(mcp_protocol STATIC)
    target_sources(mcp_protocol PRIVATE protocol/src/serialization.cpp)
    target_include_directories(mcp_protocol PUBLIC
        protocol/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    )
    target_link_libraries(mcp_protocol PUBLIC mcp_core jsonrpcpp)
endif()

if(MCP_ENABLE_CLIENT)
    add_library(mcp_client STATIC
        client/src/client.cpp
        client/src/http_transport.cpp
        client/src/process_stdio_transport.cpp
        client/src/stdio_transport.cpp
    )
    target_include_directories(mcp_client PUBLIC client/include)
    target_link_libraries(mcp_client PUBLIC mcp_core mcp_protocol cpp_httplib)
endif()

if(MCP_ENABLE_SERVER)
    add_library(mcp_server STATIC
        server/src/http_transport.cpp
        server/src/registry.cpp
        server/src/server.cpp
        server/src/stdio_transport.cpp
    )
    target_include_directories(mcp_server PUBLIC server/include)
    target_link_libraries(mcp_server PUBLIC mcp_core mcp_protocol cpp_httplib)
endif()

if(TARGET mcp_protocol)
    add_library(mcp_plugin_sdk INTERFACE)
    target_include_directories(mcp_plugin_sdk INTERFACE plugin-sdk/include)
    target_link_libraries(mcp_plugin_sdk INTERFACE mcp_core mcp_protocol)
endif()

if(TARGET mcp_server AND TARGET mcp_plugin_sdk)
    add_library(mcp_adapters INTERFACE)
    target_include_directories(mcp_adapters INTERFACE adapters/include)
    target_link_libraries(mcp_adapters INTERFACE mcp_core mcp_protocol mcp_server mcp_plugin_sdk)
endif()

add_library(mcp_observability INTERFACE)
target_include_directories(mcp_observability INTERFACE observability/include)
target_link_libraries(mcp_observability INTERFACE spdlog::spdlog_header_only)

if(MCP_ENABLE_APP)
    add_library(mcp_app STATIC
        app/src/client_discovery.cpp
        app/src/exposure_management.cpp
        app/src/file_stores.cpp
        app/src/serialization.cpp
        app/src/upstream_client.cpp
        app/src/server_management.cpp
        app/src/services.cpp
        app/src/tool_management.cpp
    )
    target_include_directories(mcp_app PUBLIC app/include)
    target_link_libraries(mcp_app PUBLIC mcp_core mcp_protocol mcp_client)
endif()

if(MCP_ENABLE_GATEWAY)
    add_library(mcp_gateway STATIC
        app/src/client_config.cpp
        app/src/gateway.cpp
        app/src/gateway_public.cpp
        app/src/gateway_onboarding.cpp
        app/src/gateway_runtime.cpp
    )
    target_include_directories(mcp_gateway PUBLIC app/include)
    target_link_libraries(mcp_gateway PUBLIC mcp_core mcp_app mcp_client mcp_server)
endif()

if(MCP_ENABLE_CLI)
    add_library(mcp_cli STATIC
        cli/src/commands.cpp
    )
    target_include_directories(mcp_cli PUBLIC cli/include)
    target_link_libraries(mcp_cli PUBLIC mcp_core mcp_app mcp_gateway mcp_client mcp_server mcp_plugin_sdk)

    add_executable(mcp_cli_app
        cli/src/main.cpp
    )
    target_link_libraries(mcp_cli_app PRIVATE mcp_cli)
    target_compile_definitions(mcp_cli_app PRIVATE MCP_PROJECT_VERSION="${PROJECT_VERSION}")
    set_target_properties(mcp_cli_app PROPERTIES OUTPUT_NAME cxxmcp)
endif()

if(MCP_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(MCP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
