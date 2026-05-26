if(NOT TARGET mcp_client AND NOT TARGET mcp_server)
    return()
endif()

function(cxxmcp_example target_name)
    target_compile_features("${target_name}" PRIVATE cxx_std_20)
endfunction()

if(TARGET mcp_server)
    add_executable(mcp_example_stdio_server stdio_server.cpp)
    target_link_libraries(mcp_example_stdio_server PRIVATE mcp_server)
    cxxmcp_example(mcp_example_stdio_server)

    add_executable(mcp_example_typed_stdio_server typed_stdio_server.cpp)
    target_link_libraries(mcp_example_typed_stdio_server PRIVATE mcp_server)
    cxxmcp_example(mcp_example_typed_stdio_server)
endif()

if(TARGET mcp_client AND TARGET mcp_server)
    add_executable(mcp_example_server_peer server_peer.cpp)
    target_link_libraries(mcp_example_server_peer PRIVATE mcp_client
                                                          mcp_server)
    cxxmcp_example(mcp_example_server_peer)

    add_executable(mcp_example_handler_contracts handler_contracts.cpp)
    target_link_libraries(mcp_example_handler_contracts PRIVATE mcp_client
                                                                mcp_server)
    cxxmcp_example(mcp_example_handler_contracts)
endif()

if(TARGET mcp_client AND TARGET mcp_server)
    add_executable(mcp_example_client_peer client_peer.cpp)
    target_link_libraries(mcp_example_client_peer PRIVATE mcp_client
                                                          mcp_server)
    cxxmcp_example(mcp_example_client_peer)

    add_executable(mcp_example_elicitation_client elicitation_client.cpp)
    target_link_libraries(mcp_example_elicitation_client PRIVATE mcp_client
                                                                 mcp_server)
    cxxmcp_example(mcp_example_elicitation_client)
endif()

if(TARGET mcp_client AND TARGET mcp_server)
    add_executable(mcp_example_client_loopback client_loopback.cpp)
    target_link_libraries(mcp_example_client_loopback PRIVATE mcp_client
                                                          mcp_server)
    cxxmcp_example(mcp_example_client_loopback)

    add_executable(mcp_example_task_async_client_server
                   task_async_client_server.cpp)
    target_link_libraries(mcp_example_task_async_client_server
                          PRIVATE mcp_client mcp_server)
    cxxmcp_example(mcp_example_task_async_client_server)
endif()

if(TARGET mcp_client AND TARGET mcp_server AND TARGET mcp_example_stdio_server)
    add_executable(mcp_example_process_stdio_client process_stdio_client.cpp)
    target_link_libraries(mcp_example_process_stdio_client PRIVATE mcp_client
                                                                  mcp_server)
    cxxmcp_example(mcp_example_process_stdio_client)
    target_compile_definitions(
        mcp_example_process_stdio_client
        PRIVATE MCP_EXAMPLE_CHILD_EXE="$<TARGET_FILE:mcp_example_stdio_server>")
    add_dependencies(mcp_example_process_stdio_client
                     mcp_example_stdio_server)
endif()

if(TARGET mcp_client AND TARGET mcp_server)
    add_executable(mcp_example_streamable_http_client
                   streamable_http_client.cpp)
    target_link_libraries(mcp_example_streamable_http_client PRIVATE
                          mcp_client mcp_server)
    cxxmcp_example(mcp_example_streamable_http_client)
endif()

if(TARGET mcp_app AND TARGET mcp_gateway)
    add_executable(mcp_example_gateway_runtime gateway_runtime.cpp)
    target_link_libraries(mcp_example_gateway_runtime PRIVATE mcp_gateway)
    cxxmcp_example(mcp_example_gateway_runtime)
endif()
