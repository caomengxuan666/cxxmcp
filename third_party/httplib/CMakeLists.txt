cmake_minimum_required(VERSION 3.14)

file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/httplib.h" _httplib_version_line
    REGEX "CPPHTTPLIB_VERSION \"([0-9]+\\.[0-9]+\\.[0-9]+)\"")
string(REGEX MATCH "([0-9]+\\.[0-9]+\\.[0-9]+)" _httplib_version
    "${_httplib_version_line}")

project(httplib
    VERSION "${_httplib_version}"
    LANGUAGES CXX
)

option(HTTPLIB_REQUIRE_OPENSSL
    "Require OpenSSL support for the vendored cpp-httplib target" OFF)
option(HTTPLIB_USE_OPENSSL_IF_AVAILABLE
    "Enable OpenSSL support for the vendored cpp-httplib target when available" OFF)

add_library(httplib INTERFACE)
add_library(httplib::httplib ALIAS httplib)

target_include_directories(httplib SYSTEM INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(httplib INTERFACE cxx_std_11)
target_link_libraries(httplib INTERFACE
    $<$<PLATFORM_ID:Windows>:ws2_32>
    $<$<PLATFORM_ID:Windows>:crypt32>
)

if(HTTPLIB_REQUIRE_OPENSSL)
    find_package(OpenSSL REQUIRED COMPONENTS SSL Crypto)
elseif(HTTPLIB_USE_OPENSSL_IF_AVAILABLE)
    find_package(OpenSSL QUIET COMPONENTS SSL Crypto)
endif()

if(TARGET OpenSSL::SSL AND TARGET OpenSSL::Crypto)
    target_compile_definitions(httplib INTERFACE CPPHTTPLIB_OPENSSL_SUPPORT)
    target_link_libraries(httplib INTERFACE OpenSSL::SSL OpenSSL::Crypto)
endif()
