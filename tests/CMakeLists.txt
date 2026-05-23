add_executable(mcp_protocol_tests
    protocol/test_protocol.cpp
)

target_link_libraries(mcp_protocol_tests PRIVATE mcp_protocol)

target_compile_definitions(mcp_protocol_tests PRIVATE
    MCP_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

add_test(NAME protocol COMMAND mcp_protocol_tests)

add_executable(mcp_client_server_tests
    client_server/test_client_server.cpp
)

target_link_libraries(mcp_client_server_tests PRIVATE mcp_client mcp_server)

add_test(NAME client_server COMMAND mcp_client_server_tests)

add_executable(mcp_stdio_transport_tests
    transport/test_stdio_transport.cpp
)

target_link_libraries(mcp_stdio_transport_tests PRIVATE mcp_client mcp_server)

add_test(NAME stdio_transport COMMAND mcp_stdio_transport_tests)

add_executable(mcp_app_tests
    app/test_app.cpp
)

target_link_libraries(mcp_app_tests PRIVATE mcp_app)

add_test(NAME app COMMAND mcp_app_tests)

add_executable(mcp_cli_tests
    cli/test_cli.cpp
)

target_link_libraries(mcp_cli_tests PRIVATE mcp_cli)

add_test(NAME cli COMMAND mcp_cli_tests)

add_executable(mcp_gui_tests
    gui/test_gui_controller.cpp
)

target_link_libraries(mcp_gui_tests PRIVATE mcp_gui)

add_test(NAME gui COMMAND mcp_gui_tests)

add_executable(mcp_tool_management_tests
    app/test_tool_management.cpp
)

target_link_libraries(mcp_tool_management_tests PRIVATE mcp_app)

add_test(NAME tool_management COMMAND mcp_tool_management_tests)
