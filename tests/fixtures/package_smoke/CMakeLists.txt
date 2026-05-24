cmake_minimum_required(VERSION 3.23)
project(cxxmcp_package_smoke_consumer LANGUAGES CXX)

find_package(cxxmcp CONFIG REQUIRED)

add_executable(cxxmcp_package_smoke main.cpp)
target_link_libraries(cxxmcp_package_smoke PRIVATE
    cxxmcp::sdk
    cxxmcp::plugin_sdk
    cxxmcp::adapters
)
