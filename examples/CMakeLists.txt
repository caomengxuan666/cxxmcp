if(TARGET mcp_server)
    add_executable(mcp_example_stdio_server
        stdio_server.cpp
    )
    target_link_libraries(mcp_example_stdio_server PRIVATE mcp_server)
    set_target_properties(mcp_example_stdio_server PROPERTIES OUTPUT_NAME cxxmcp-example-stdio-server)
endif()

if(TARGET mcp_client AND TARGET mcp_server)
    add_executable(mcp_example_client
        client_loopback.cpp
    )
    target_link_libraries(mcp_example_client PRIVATE mcp_client mcp_server)
    set_target_properties(mcp_example_client PROPERTIES OUTPUT_NAME cxxmcp-example-client)
endif()

if(TARGET mcp_client AND TARGET mcp_example_stdio_server)
    add_executable(mcp_example_process_stdio_client
        process_stdio_client.cpp
    )
    target_link_libraries(mcp_example_process_stdio_client PRIVATE mcp_client)
    target_compile_definitions(mcp_example_process_stdio_client PRIVATE
        MCP_EXAMPLE_CHILD_EXE="$<TARGET_FILE:mcp_example_stdio_server>"
    )
    add_dependencies(mcp_example_process_stdio_client mcp_example_stdio_server)
    set_target_properties(mcp_example_process_stdio_client PROPERTIES OUTPUT_NAME cxxmcp-example-process-stdio-client)
endif()

if(TARGET mcp_gateway AND TARGET mcp_client AND TARGET mcp_example_stdio_server)
    add_executable(mcp_example_gateway
        gateway_runtime.cpp
    )
    target_link_libraries(mcp_example_gateway PRIVATE mcp_gateway mcp_client)
    target_compile_definitions(mcp_example_gateway PRIVATE
        MCP_EXAMPLE_CHILD_EXE="$<TARGET_FILE:mcp_example_stdio_server>"
    )
    add_dependencies(mcp_example_gateway mcp_example_stdio_server)
    set_target_properties(mcp_example_gateway PROPERTIES OUTPUT_NAME cxxmcp-example-gateway)
endif()
