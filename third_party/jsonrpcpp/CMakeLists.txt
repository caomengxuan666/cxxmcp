cmake_minimum_required(VERSION 3.14)

add_library(jsonrpcpp INTERFACE)
target_include_directories(jsonrpcpp INTERFACE ${CMAKE_CURRENT_LIST_DIR}/..)
target_compile_features(jsonrpcpp INTERFACE cxx_std_11)
