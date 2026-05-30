if(NOT MCP_ENABLE_HTTP OR NOT TARGET mcp_client OR NOT TARGET mcp_server)
    return()
endif()

add_executable(mcp_bench_echo_server echo_server.cpp)
target_link_libraries(mcp_bench_echo_server PRIVATE mcp_client mcp_server)
target_compile_features(mcp_bench_echo_server PRIVATE cxx_std_20)
