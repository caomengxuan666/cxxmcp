cmake_minimum_required(VERSION 3.23)
project(cxxmcp_external_consumer LANGUAGES CXX)

find_package(cxxmcp CONFIG REQUIRED)

add_executable(cxxmcp_external_consumer main.cpp)
target_compile_features(cxxmcp_external_consumer PRIVATE cxx_std_17)
target_link_libraries(cxxmcp_external_consumer PRIVATE cxxmcp::sdk)
