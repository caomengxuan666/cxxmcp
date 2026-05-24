cmake_minimum_required(VERSION 3.23)

project(cxxmcp
    VERSION 2.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries by default" FORCE)

include(CTest)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

function(cxxmcp_build_option canonical legacy description default_value)
    if(DEFINED ${legacy} AND NOT DEFINED ${canonical})
        set(${canonical} "${${legacy}}" CACHE BOOL "${description}" FORCE)
    endif()
    option(${canonical} "${description}" "${default_value}")
    set(${legacy} "${${canonical}}" CACHE BOOL
        "Deprecated alias for ${canonical}" FORCE)
    mark_as_advanced(${legacy})
endfunction()

cxxmcp_build_option(CXXMCP_BUILD_SDK MCP_BUILD_SDK
    "Build the public SDK layer" ON)
cxxmcp_build_option(CXXMCP_BUILD_PROTOCOL MCP_BUILD_PROTOCOL
    "Build the MCP protocol library" ON)
cxxmcp_build_option(CXXMCP_BUILD_CLIENT MCP_BUILD_CLIENT
    "Build the MCP client library" OFF)
cxxmcp_build_option(CXXMCP_BUILD_SERVER MCP_BUILD_SERVER
    "Build the MCP server library" OFF)
cxxmcp_build_option(CXXMCP_BUILD_RUNTIME MCP_BUILD_RUNTIME
    "Build the runtime application layer" OFF)
cxxmcp_build_option(CXXMCP_BUILD_APP MCP_BUILD_APP
    "Build the runtime application service library" OFF)
cxxmcp_build_option(CXXMCP_BUILD_GATEWAY MCP_BUILD_GATEWAY
    "Build the gateway service library" OFF)
cxxmcp_build_option(CXXMCP_BUILD_CLI MCP_BUILD_CLI
    "Build the command-line application" OFF)
cxxmcp_build_option(CXXMCP_BUILD_EXAMPLES MCP_BUILD_EXAMPLES
    "Build example executables" OFF)
cxxmcp_build_option(CXXMCP_BUILD_TESTS MCP_BUILD_TESTS
    "Build tests for enabled layers" ${BUILD_TESTING})
cxxmcp_build_option(CXXMCP_BUILD_DOCS MCP_BUILD_DOCS
    "Build Doxygen API documentation" OFF)

unset(CXXMCP_BUILD_GUI CACHE)
unset(MCP_BUILD_GUI CACHE)

set(MCP_ENABLE_PROTOCOL ${CXXMCP_BUILD_PROTOCOL})
set(MCP_ENABLE_CLIENT ${CXXMCP_BUILD_CLIENT})
set(MCP_ENABLE_SERVER ${CXXMCP_BUILD_SERVER})
set(MCP_ENABLE_RUNTIME ${CXXMCP_BUILD_RUNTIME})
set(MCP_ENABLE_APP ${CXXMCP_BUILD_APP})
set(MCP_ENABLE_GATEWAY ${CXXMCP_BUILD_GATEWAY})
set(MCP_ENABLE_CLI ${CXXMCP_BUILD_CLI})
set(MCP_ENABLE_SDK ${CXXMCP_BUILD_SDK})

if(MCP_ENABLE_SDK)
    set(MCP_ENABLE_PROTOCOL ON)
    set(MCP_ENABLE_CLIENT ON)
    set(MCP_ENABLE_SERVER ON)
endif()

if(CXXMCP_BUILD_CLI)
    set(MCP_ENABLE_GATEWAY ON)
endif()

if(MCP_ENABLE_GATEWAY)
    set(MCP_ENABLE_APP ON)
    set(MCP_ENABLE_SERVER ON)
endif()

if(MCP_ENABLE_RUNTIME)
    set(MCP_ENABLE_APP ON)
    set(MCP_ENABLE_GATEWAY ON)
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
set_target_properties(mcp_core PROPERTIES EXPORT_NAME core)
target_include_directories(mcp_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/core/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

add_library(jsonrpcpp INTERFACE)
target_include_directories(jsonrpcpp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/jsonrpcpp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/cxxmcp/third_party/jsonrpcpp>
)

add_library(cpp_httplib INTERFACE)
target_include_directories(cpp_httplib INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/httplib>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/cxxmcp/third_party/httplib>
)

add_subdirectory(third_party/spdlog EXCLUDE_FROM_ALL)

if(MCP_ENABLE_PROTOCOL)
    add_library(mcp_protocol STATIC)
    set_target_properties(mcp_protocol PROPERTIES EXPORT_NAME protocol)
    target_sources(mcp_protocol PRIVATE sdk/protocol/src/serialization.cpp)
    target_include_directories(mcp_protocol PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/protocol/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(mcp_protocol PUBLIC mcp_core jsonrpcpp)
endif()

if(MCP_ENABLE_CLIENT)
    add_library(mcp_client STATIC
        sdk/client/src/client.cpp
        sdk/client/src/http_transport.cpp
        sdk/client/src/process_stdio_transport.cpp
        sdk/client/src/stdio_transport.cpp
    )
    set_target_properties(mcp_client PROPERTIES EXPORT_NAME client)
    target_include_directories(mcp_client PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/client/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(mcp_client PUBLIC mcp_core mcp_protocol cpp_httplib)
endif()

if(MCP_ENABLE_SERVER)
    add_library(mcp_server STATIC
        sdk/server/src/http_transport.cpp
        sdk/server/src/registry.cpp
        sdk/server/src/server.cpp
        sdk/server/src/stdio_transport.cpp
    )
    set_target_properties(mcp_server PROPERTIES EXPORT_NAME server)
    target_include_directories(mcp_server PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/server/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(mcp_server PUBLIC mcp_core mcp_protocol cpp_httplib)
endif()

if(TARGET mcp_protocol OR TARGET mcp_client OR TARGET mcp_server)
    add_library(mcp_sdk INTERFACE)
    set_target_properties(mcp_sdk PROPERTIES EXPORT_NAME sdk)
    target_include_directories(mcp_sdk INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )

    if(TARGET mcp_protocol)
        target_link_libraries(mcp_sdk INTERFACE mcp_protocol)
    endif()
    if(TARGET mcp_client)
        target_link_libraries(mcp_sdk INTERFACE mcp_client)
    endif()
    if(TARGET mcp_server)
        target_link_libraries(mcp_sdk INTERFACE mcp_server)
    endif()
endif()

if(TARGET mcp_client AND TARGET mcp_server)
    add_library(mcp_transport INTERFACE)
    set_target_properties(mcp_transport PROPERTIES EXPORT_NAME transport)
    target_include_directories(mcp_transport INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(mcp_transport INTERFACE mcp_client mcp_server)

    add_library(mcp_peer INTERFACE)
    set_target_properties(mcp_peer PROPERTIES EXPORT_NAME peer)
    target_include_directories(mcp_peer INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(mcp_peer INTERFACE mcp_client mcp_server)

    add_library(mcp_handler INTERFACE)
    set_target_properties(mcp_handler PROPERTIES EXPORT_NAME handler)
    target_include_directories(mcp_handler INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(mcp_handler INTERFACE mcp_client mcp_server)

    add_library(mcp_service INTERFACE)
    set_target_properties(mcp_service PROPERTIES EXPORT_NAME service)
    target_include_directories(mcp_service INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(mcp_service INTERFACE mcp_peer)
endif()

if(TARGET mcp_sdk)
    add_library(cxxmcp::sdk ALIAS mcp_sdk)
endif()

if(TARGET mcp_protocol)
    add_library(cxxmcp::protocol ALIAS mcp_protocol)
endif()

if(TARGET mcp_client)
    add_library(cxxmcp::client ALIAS mcp_client)
endif()

if(TARGET mcp_server)
    add_library(cxxmcp::server ALIAS mcp_server)
endif()

if(TARGET mcp_transport)
    add_library(cxxmcp::transport ALIAS mcp_transport)
endif()

if(TARGET mcp_peer)
    add_library(cxxmcp::peer ALIAS mcp_peer)
endif()

if(TARGET mcp_handler)
    add_library(cxxmcp::handler ALIAS mcp_handler)
endif()

if(TARGET mcp_service)
    add_library(cxxmcp::service ALIAS mcp_service)
endif()

if(TARGET mcp_app)
    add_library(cxxmcp::runtime ALIAS mcp_app)
endif()

if(TARGET mcp_gateway)
    add_library(cxxmcp::gateway ALIAS mcp_gateway)
endif()

if(TARGET mcp_cli)
    add_library(cxxmcp::cli ALIAS mcp_cli)
endif()

if(TARGET mcp_protocol)
    add_library(mcp_plugin_sdk INTERFACE)
    target_include_directories(mcp_plugin_sdk INTERFACE extensions/plugin-sdk/include)
    target_link_libraries(mcp_plugin_sdk INTERFACE mcp_core mcp_protocol)
endif()

if(TARGET mcp_server AND TARGET mcp_plugin_sdk)
    add_library(mcp_adapters INTERFACE)
    target_include_directories(mcp_adapters INTERFACE extensions/adapters/include)
    target_link_libraries(mcp_adapters INTERFACE mcp_core mcp_protocol mcp_server mcp_plugin_sdk)
endif()

add_library(mcp_observability INTERFACE)
target_include_directories(mcp_observability INTERFACE runtime/observability/include)
target_link_libraries(mcp_observability INTERFACE spdlog::spdlog_header_only)

if(MCP_ENABLE_APP)
    add_library(mcp_app STATIC
        runtime/src/client_discovery.cpp
        runtime/src/exposure_management.cpp
        runtime/src/file_stores.cpp
        runtime/src/serialization.cpp
        runtime/src/task_management.cpp
        runtime/src/upstream_client.cpp
        runtime/src/server_management.cpp
        runtime/src/services.cpp
        runtime/src/tool_management.cpp
    )
    target_include_directories(mcp_app PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/runtime/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(mcp_app PUBLIC mcp_core mcp_protocol mcp_client)
endif()

if(MCP_ENABLE_GATEWAY)
    add_library(mcp_gateway STATIC
        runtime/src/client_config.cpp
        runtime/src/gateway.cpp
        runtime/src/gateway_public.cpp
        runtime/src/gateway_onboarding.cpp
        runtime/src/gateway_runtime.cpp
    )
    target_include_directories(mcp_gateway PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/runtime/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(mcp_gateway PUBLIC mcp_core mcp_app mcp_client mcp_server)
endif()

if(MCP_ENABLE_CLI)
    set(CLI11_BUILD_DOCS OFF CACHE BOOL "Disable CLI11 docs" FORCE)
    set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "Disable CLI11 examples" FORCE)
    set(CLI11_BUILD_EXAMPLES_JSON OFF CACHE BOOL "Disable CLI11 json examples" FORCE)
    set(CLI11_BUILD_TESTS OFF CACHE BOOL "Disable CLI11 tests" FORCE)
    set(CLI11_INSTALL OFF CACHE BOOL "Disable CLI11 install rules" FORCE)
    set(CLI11_SINGLE_FILE OFF CACHE BOOL "Disable CLI11 single-file build" FORCE)
    add_subdirectory(third_party/CLI11 EXCLUDE_FROM_ALL)

    add_library(mcp_cli STATIC
        tools/cli/src/commands.cpp
    )
    target_include_directories(mcp_cli PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tools/cli/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(mcp_cli PUBLIC mcp_core mcp_app mcp_gateway mcp_client mcp_server mcp_plugin_sdk CLI11::CLI11)

    add_executable(mcp_cli_app
        tools/cli/src/main.cpp
    )
    target_link_libraries(mcp_cli_app PRIVATE mcp_cli)
    target_compile_definitions(mcp_cli_app PRIVATE MCP_PROJECT_VERSION="${PROJECT_VERSION}")
    set_target_properties(mcp_cli_app PROPERTIES OUTPUT_NAME cxxmcp)
endif()

if(CXXMCP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(CXXMCP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(CXXMCP_BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating Doxygen API documentation"
        VERBATIM
    )
endif()

set(CXXMCP_INSTALL_TARGETS mcp_core jsonrpcpp cpp_httplib)
if(TARGET mcp_protocol)
    list(APPEND CXXMCP_INSTALL_TARGETS mcp_protocol)
endif()
if(TARGET mcp_client)
    list(APPEND CXXMCP_INSTALL_TARGETS mcp_client)
endif()
if(TARGET mcp_server)
    list(APPEND CXXMCP_INSTALL_TARGETS mcp_server)
endif()
if(TARGET mcp_sdk)
    list(APPEND CXXMCP_INSTALL_TARGETS mcp_sdk)
endif()
if(TARGET mcp_transport)
    list(APPEND CXXMCP_INSTALL_TARGETS mcp_transport)
endif()
if(TARGET mcp_peer)
    list(APPEND CXXMCP_INSTALL_TARGETS mcp_peer)
endif()
if(TARGET mcp_handler)
    list(APPEND CXXMCP_INSTALL_TARGETS mcp_handler)
endif()
if(TARGET mcp_service)
    list(APPEND CXXMCP_INSTALL_TARGETS mcp_service)
endif()
if(TARGET mcp_app)
    list(APPEND CXXMCP_INSTALL_TARGETS mcp_app)
endif()
if(TARGET mcp_gateway)
    list(APPEND CXXMCP_INSTALL_TARGETS mcp_gateway)
endif()
if(TARGET mcp_cli)
    list(APPEND CXXMCP_INSTALL_TARGETS mcp_cli)
endif()

install(TARGETS ${CXXMCP_INSTALL_TARGETS}
    EXPORT cxxmcpTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY sdk/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY sdk/core/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(TARGET mcp_protocol)
    install(DIRECTORY sdk/protocol/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
if(TARGET mcp_client)
    install(DIRECTORY sdk/client/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
if(TARGET mcp_server)
    install(DIRECTORY sdk/server/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
install(FILES
    third_party/tl/expected.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tl
)

install(FILES
    third_party/jsonrpcpp/jsonrpcpp.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cxxmcp/third_party/jsonrpcpp
)
install(FILES
    third_party/httplib/httplib.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cxxmcp/third_party/httplib
)
install(FILES
    third_party/nlohmann/json.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nlohmann
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/cxxmcpConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cxxmcpConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cxxmcpConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cxxmcp
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cxxmcpConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cxxmcpConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cxxmcp
)

install(EXPORT cxxmcpTargets
    NAMESPACE cxxmcp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cxxmcp
)
