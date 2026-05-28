cmake_minimum_required(VERSION 3.23)

project(cxxmcp
    VERSION 2.0.2
    LANGUAGES CXX
)

set(CMAKE_CXX_EXTENSIONS OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

include(CTest)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

find_package(Threads REQUIRED)

function(cxxmcp_build_option canonical legacy description default_value)
    if(DEFINED ${legacy} AND NOT DEFINED ${canonical})
        set(${canonical} "${${legacy}}" CACHE BOOL "${description}" FORCE)
    endif()
    option(${canonical} "${description}" "${default_value}")
    set(${legacy} "${${canonical}}" CACHE BOOL
        "Deprecated alias for ${canonical}" FORCE)
    mark_as_advanced(${legacy})
endfunction()

set(CXXMCP_SDK_CXX_STANDARD "17" CACHE STRING
    "Minimum C++ standard required by exported cxxmcp SDK targets")
set_property(CACHE CXXMCP_SDK_CXX_STANDARD PROPERTY STRINGS 17 20 23 26)
if(NOT CXXMCP_SDK_CXX_STANDARD MATCHES "^(17|20|23|26)$")
    message(FATAL_ERROR
        "CXXMCP_SDK_CXX_STANDARD must be one of 17, 20, 23, or 26")
endif()

function(cxxmcp_require_cxx_standard target_name standard)
    get_target_property(target_type "${target_name}" TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        target_compile_features("${target_name}" INTERFACE cxx_std_${standard})
    else()
        target_compile_features("${target_name}" PUBLIC cxx_std_${standard})
    endif()
endfunction()

function(cxxmcp_require_sdk_cxx target_name)
    cxxmcp_require_cxx_standard("${target_name}" "${CXXMCP_SDK_CXX_STANDARD}")
endfunction()

function(cxxmcp_require_cxx20 target_name)
    cxxmcp_require_cxx_standard("${target_name}" "20")
endfunction()

set(CXXMCP_MSVC_RUNTIME_LIBRARY "" CACHE STRING
    "MSVC runtime library for cxxmcp compiled targets; empty keeps the toolchain default")
set_property(CACHE CXXMCP_MSVC_RUNTIME_LIBRARY PROPERTY STRINGS
    ""
    "MultiThreaded"
    "MultiThreadedDebug"
    "MultiThreadedDLL"
    "MultiThreadedDebugDLL"
    "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

if(MSVC AND CXXMCP_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "${CXXMCP_MSVC_RUNTIME_LIBRARY}")
endif()

if(MSVC)
    add_compile_options(/bigobj)
elseif(MINGW)
    add_compile_options(-Wa,-mbig-obj)
endif()

cxxmcp_build_option(CXXMCP_BUILD_SDK MCP_BUILD_SDK
    "Build the public SDK layer" ON)
cxxmcp_build_option(CXXMCP_BUILD_PROTOCOL MCP_BUILD_PROTOCOL
    "Build the MCP protocol library" ON)
cxxmcp_build_option(CXXMCP_BUILD_CLIENT MCP_BUILD_CLIENT
    "Build the MCP client library" OFF)
cxxmcp_build_option(CXXMCP_BUILD_SERVER MCP_BUILD_SERVER
    "Build the MCP server library" OFF)
cxxmcp_build_option(CXXMCP_BUILD_EXAMPLES MCP_BUILD_EXAMPLES
    "Build example executables" OFF)
cxxmcp_build_option(CXXMCP_BUILD_TESTS MCP_BUILD_TESTS
    "Build tests for enabled layers" ${BUILD_TESTING})
cxxmcp_build_option(CXXMCP_BUILD_DOCS MCP_BUILD_DOCS
    "Build Doxygen API documentation" OFF)
cxxmcp_build_option(CXXMCP_ENABLE_AUTH MCP_ENABLE_AUTH
    "Build optional OAuth 2.1 / DPoP authorization scaffolding" OFF)
cxxmcp_build_option(CXXMCP_ENABLE_PLUGIN_SDK MCP_ENABLE_PLUGIN_SDK
    "Build optional plugin SDK extension target" OFF)
cxxmcp_build_option(CXXMCP_ENABLE_ADAPTERS MCP_ENABLE_ADAPTERS
    "Build optional adapter extension target" OFF)

set(CXXMCP_AUTH_CRYPTO "NONE" CACHE STRING
    "Optional crypto backend for cxxmcp auth; NONE or OpenSSL")
set_property(CACHE CXXMCP_AUTH_CRYPTO PROPERTY STRINGS NONE OpenSSL)
if(NOT CXXMCP_AUTH_CRYPTO MATCHES "^(NONE|OpenSSL)$")
    message(FATAL_ERROR "CXXMCP_AUTH_CRYPTO must be NONE or OpenSSL")
endif()

option(CXXMCP_USE_SYSTEM_DEPS
    "Use package-manager provided SDK dependencies instead of bundled headers"
    OFF)
option(CXXMCP_USE_SYSTEM_JSONRPCPP
    "Use a package-manager provided jsonrpcpp header instead of the bundled private header"
    OFF)

set(MCP_ENABLE_PROTOCOL ${CXXMCP_BUILD_PROTOCOL})
set(MCP_ENABLE_CLIENT ${CXXMCP_BUILD_CLIENT})
set(MCP_ENABLE_SERVER ${CXXMCP_BUILD_SERVER})
set(MCP_ENABLE_SDK ${CXXMCP_BUILD_SDK})
set(MCP_ENABLE_AUTH ${CXXMCP_ENABLE_AUTH})
set(MCP_ENABLE_PLUGIN_SDK ${CXXMCP_ENABLE_PLUGIN_SDK})
set(MCP_ENABLE_ADAPTERS ${CXXMCP_ENABLE_ADAPTERS})

if(MCP_ENABLE_SDK)
    set(MCP_ENABLE_PROTOCOL ON)
    set(MCP_ENABLE_CLIENT ON)
    set(MCP_ENABLE_SERVER ON)
endif()

if(MCP_ENABLE_CLIENT OR MCP_ENABLE_SERVER)
    set(MCP_ENABLE_PROTOCOL ON)
endif()

if(MCP_ENABLE_ADAPTERS AND NOT MCP_ENABLE_PLUGIN_SDK)
    message(FATAL_ERROR
        "CXXMCP_ENABLE_ADAPTERS requires CXXMCP_ENABLE_PLUGIN_SDK")
endif()

if(NOT MCP_ENABLE_AUTH AND NOT CXXMCP_AUTH_CRYPTO STREQUAL "NONE")
    message(FATAL_ERROR
        "CXXMCP_AUTH_CRYPTO requires CXXMCP_ENABLE_AUTH=ON")
endif()

add_library(mcp_core INTERFACE)
set_target_properties(mcp_core PROPERTIES EXPORT_NAME core)
target_include_directories(mcp_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/core/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(CXXMCP_USE_SYSTEM_DEPS)
    find_package(tl-expected CONFIG REQUIRED)
    target_link_libraries(mcp_core INTERFACE tl::expected)
else()
    target_include_directories(mcp_core INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party>
    )
endif()

cxxmcp_require_sdk_cxx(mcp_core)

if(CXXMCP_USE_SYSTEM_DEPS)
    find_package(nlohmann_json CONFIG REQUIRED)
    find_package(httplib CONFIG REQUIRED)

    set(CXXMCP_JSON_TARGET nlohmann_json::nlohmann_json)
    set(CXXMCP_HTTPLIB_TARGET httplib::httplib)
else()
    set(CXXMCP_JSON_TARGET "")
    set(CXXMCP_HTTPLIB_TARGET "")
endif()

if(CXXMCP_USE_SYSTEM_DEPS)
    if(CXXMCP_USE_SYSTEM_JSONRPCPP)
        find_path(CXXMCP_JSONRPCPP_INCLUDE_DIR
            NAMES jsonrpcpp.hpp
            PATH_SUFFIXES jsonrpcpp
            REQUIRED
        )
        set(CXXMCP_JSONRPCPP_INCLUDE_DIRS "${CXXMCP_JSONRPCPP_INCLUDE_DIR}")
    else()
        set(CXXMCP_JSONRPCPP_INCLUDE_DIRS
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/jsonrpcpp")
    endif()
else()
    set(CXXMCP_JSONRPCPP_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/jsonrpcpp")
endif()

# Internal implementation dependency for HTTP transports and tests. It is not
# installed or exported as part of the public SDK package contract.
add_library(cpp_httplib INTERFACE)
if(CXXMCP_USE_SYSTEM_DEPS)
    target_link_libraries(cpp_httplib INTERFACE "${CXXMCP_HTTPLIB_TARGET}")
else()
    target_include_directories(cpp_httplib INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/httplib>
    )
endif()
target_link_libraries(cpp_httplib INTERFACE
    $<$<PLATFORM_ID:Windows>:ws2_32>
)

cxxmcp_require_sdk_cxx(cpp_httplib)

if(MCP_ENABLE_PROTOCOL)
    add_library(mcp_protocol STATIC)
    set_target_properties(mcp_protocol PROPERTIES EXPORT_NAME protocol)
    target_sources(mcp_protocol PRIVATE sdk/protocol/src/serialization.cpp)
    target_include_directories(mcp_protocol PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/protocol/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    if(NOT CXXMCP_USE_SYSTEM_DEPS)
        target_include_directories(mcp_protocol PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party>
        )
    endif()
    target_include_directories(mcp_protocol PRIVATE
        ${CXXMCP_JSONRPCPP_INCLUDE_DIRS}
    )
    target_link_libraries(mcp_protocol PUBLIC mcp_core)
    if(CXXMCP_JSON_TARGET)
        target_link_libraries(mcp_protocol PUBLIC "${CXXMCP_JSON_TARGET}")
    endif()
    cxxmcp_require_sdk_cxx(mcp_protocol)
endif()

if(MCP_ENABLE_CLIENT)
    add_library(mcp_client STATIC
        sdk/client/src/client.cpp
        sdk/client/src/http_transport.cpp
        sdk/client/src/process_stdio_transport.cpp
        sdk/client/src/stdio_transport.cpp
        sdk/client/src/transport_adapter.cpp
    )
    set_target_properties(mcp_client PROPERTIES EXPORT_NAME client)
    target_include_directories(mcp_client PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/client/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    if(NOT CXXMCP_USE_SYSTEM_DEPS)
        target_include_directories(mcp_client PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/httplib
        )
    endif()
    target_link_libraries(mcp_client
        PUBLIC
            mcp_core
            mcp_protocol
            $<$<PLATFORM_ID:Windows>:ws2_32>
        PRIVATE
            Threads::Threads
            ${CXXMCP_HTTPLIB_TARGET}
    )
    cxxmcp_require_sdk_cxx(mcp_client)
endif()

if(MCP_ENABLE_SERVER)
    add_library(mcp_server STATIC
        sdk/server/src/http_transport.cpp
        sdk/server/src/registry.cpp
        sdk/server/src/server.cpp
        sdk/server/src/stdio_transport.cpp
        sdk/server/src/task_manager.cpp
        sdk/server/src/transport_adapter.cpp
    )
    set_target_properties(mcp_server PROPERTIES EXPORT_NAME server)
    target_include_directories(mcp_server PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/server/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    if(NOT CXXMCP_USE_SYSTEM_DEPS)
        target_include_directories(mcp_server PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/httplib
        )
    endif()
    target_link_libraries(mcp_server
        PUBLIC
            mcp_core
            mcp_protocol
            $<$<PLATFORM_ID:Windows>:ws2_32>
        PRIVATE
            ${CXXMCP_HTTPLIB_TARGET}
    )
    cxxmcp_require_sdk_cxx(mcp_server)
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
    cxxmcp_require_sdk_cxx(mcp_sdk)
endif()

if(TARGET mcp_protocol)
    add_library(mcp_transport INTERFACE)
    set_target_properties(mcp_transport PROPERTIES EXPORT_NAME transport)
    target_include_directories(mcp_transport INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/transport/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(mcp_transport INTERFACE mcp_core mcp_protocol)
    cxxmcp_require_sdk_cxx(mcp_transport)
endif()

if(MCP_ENABLE_AUTH)
    add_library(mcp_auth INTERFACE)
    set_target_properties(mcp_auth PROPERTIES EXPORT_NAME auth)
    target_include_directories(mcp_auth INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/auth/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    if(NOT CXXMCP_USE_SYSTEM_DEPS)
        target_include_directories(mcp_auth INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party>
        )
    endif()
    target_link_libraries(mcp_auth INTERFACE mcp_core)
    if(CXXMCP_JSON_TARGET)
        target_link_libraries(mcp_auth INTERFACE "${CXXMCP_JSON_TARGET}")
    endif()
    target_compile_definitions(mcp_auth INTERFACE CXXMCP_ENABLE_AUTH=1)
    cxxmcp_require_sdk_cxx(mcp_auth)

    if(TARGET mcp_server)
        target_link_libraries(mcp_server PUBLIC mcp_auth)
    endif()
endif()

if(TARGET mcp_auth AND CXXMCP_AUTH_CRYPTO STREQUAL "OpenSSL")
    find_package(OpenSSL REQUIRED)
    add_library(mcp_auth_openssl INTERFACE)
    set_target_properties(mcp_auth_openssl PROPERTIES EXPORT_NAME auth_openssl)
    target_include_directories(mcp_auth_openssl INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/auth/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(mcp_auth_openssl INTERFACE mcp_auth OpenSSL::Crypto)
    cxxmcp_require_sdk_cxx(mcp_auth_openssl)
endif()

if(TARGET mcp_sdk AND TARGET mcp_transport)
    target_link_libraries(mcp_sdk INTERFACE mcp_transport)
endif()

if(TARGET mcp_client AND TARGET mcp_transport)
    target_link_libraries(mcp_client PUBLIC mcp_transport)
endif()

if(TARGET mcp_server AND TARGET mcp_transport)
    target_link_libraries(mcp_server PUBLIC mcp_transport)
endif()

if(TARGET mcp_client AND TARGET mcp_server)
    add_library(mcp_peer INTERFACE)
    set_target_properties(mcp_peer PROPERTIES EXPORT_NAME peer)
    target_include_directories(mcp_peer INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(mcp_peer INTERFACE mcp_client mcp_server)
    cxxmcp_require_sdk_cxx(mcp_peer)

    add_library(mcp_handler INTERFACE)
    set_target_properties(mcp_handler PROPERTIES EXPORT_NAME handler)
    target_include_directories(mcp_handler INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(mcp_handler INTERFACE mcp_client mcp_server)
    cxxmcp_require_sdk_cxx(mcp_handler)

    add_library(mcp_service INTERFACE)
    set_target_properties(mcp_service PROPERTIES EXPORT_NAME service)
    target_include_directories(mcp_service INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(mcp_service INTERFACE mcp_peer)
    cxxmcp_require_sdk_cxx(mcp_service)
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

if(TARGET mcp_auth)
    add_library(cxxmcp::auth ALIAS mcp_auth)
endif()

if(TARGET mcp_auth_openssl)
    add_library(cxxmcp::auth_openssl ALIAS mcp_auth_openssl)
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

if(MCP_ENABLE_PLUGIN_SDK AND TARGET mcp_protocol)
    add_library(mcp_plugin_sdk INTERFACE)
    target_include_directories(mcp_plugin_sdk INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/extensions/plugin-sdk/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(mcp_plugin_sdk INTERFACE mcp_core mcp_protocol)
    set_target_properties(mcp_plugin_sdk PROPERTIES EXPORT_NAME plugin_sdk)
    cxxmcp_require_sdk_cxx(mcp_plugin_sdk)
    add_library(cxxmcp::plugin_sdk ALIAS mcp_plugin_sdk)
endif()

if(MCP_ENABLE_ADAPTERS AND TARGET mcp_server AND TARGET mcp_plugin_sdk)
    add_library(mcp_adapters INTERFACE)
    target_include_directories(mcp_adapters INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/extensions/adapters/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(mcp_adapters INTERFACE mcp_core mcp_protocol mcp_server mcp_plugin_sdk)
    set_target_properties(mcp_adapters PROPERTIES EXPORT_NAME adapters)
    cxxmcp_require_sdk_cxx(mcp_adapters)
    add_library(cxxmcp::adapters ALIAS mcp_adapters)
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

set(CXXMCP_INSTALL_TARGETS mcp_core)
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
if(TARGET mcp_auth)
    list(APPEND CXXMCP_INSTALL_TARGETS mcp_auth)
endif()
if(TARGET mcp_auth_openssl)
    list(APPEND CXXMCP_INSTALL_TARGETS mcp_auth_openssl)
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
if(TARGET mcp_plugin_sdk)
    list(APPEND CXXMCP_INSTALL_TARGETS mcp_plugin_sdk)
endif()
if(TARGET mcp_adapters)
    list(APPEND CXXMCP_INSTALL_TARGETS mcp_adapters)
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
if(TARGET mcp_transport)
    install(DIRECTORY sdk/transport/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
if(TARGET mcp_auth)
    install(DIRECTORY sdk/auth/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
if(TARGET mcp_plugin_sdk)
    install(DIRECTORY extensions/plugin-sdk/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
if(TARGET mcp_adapters)
    install(DIRECTORY extensions/adapters/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
if(NOT CXXMCP_USE_SYSTEM_DEPS)
    install(FILES
        third_party/tl/expected.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tl
    )

    install(FILES
        third_party/nlohmann/json.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nlohmann
    )
endif()

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
