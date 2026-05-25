if(TARGET mcp_protocol)
    add_executable(mcp_protocol_tests
        protocol/test_protocol.cpp
    )

    target_link_libraries(mcp_protocol_tests PRIVATE mcp_protocol)

    target_compile_definitions(mcp_protocol_tests PRIVATE
        MCP_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    )

    add_test(NAME protocol COMMAND mcp_protocol_tests)
endif()

if(TARGET mcp_transport)
    add_executable(mcp_transport_contract_tests
        transport/test_transport_contract.cpp
    )

    target_link_libraries(mcp_transport_contract_tests PRIVATE mcp_transport)

    add_test(NAME transport_contract COMMAND mcp_transport_contract_tests)
endif()

if(TARGET mcp_client AND TARGET mcp_server)
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

    add_executable(mcp_http_transport_tests
        transport/test_http_transport.cpp
    )

    target_link_libraries(mcp_http_transport_tests PRIVATE mcp_client mcp_server)

    add_test(NAME http_transport COMMAND mcp_http_transport_tests)

    add_executable(mcp_rmcp_conformance_tests
        interop/test_rmcp_conformance.cpp
    )

    target_link_libraries(mcp_rmcp_conformance_tests PRIVATE mcp_client mcp_server)
    target_compile_definitions(mcp_rmcp_conformance_tests PRIVATE
        MCP_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    )

    add_test(NAME rmcp_conformance COMMAND mcp_rmcp_conformance_tests)

    add_executable(mcp_sdk_tests
        sdk/test_sdk.cpp
    )

    target_link_libraries(mcp_sdk_tests PRIVATE cxxmcp::sdk)

    add_test(NAME sdk COMMAND mcp_sdk_tests)

    add_executable(mcp_public_target_tests
        sdk/test_public_targets.cpp
    )

    target_link_libraries(mcp_public_target_tests PRIVATE
        cxxmcp::handler
        cxxmcp::service
    )

    add_test(NAME public_targets COMMAND mcp_public_target_tests)
endif()

if(CXXMCP_BUILD_TESTS AND CXXMCP_BUILD_SDK AND CXXMCP_BUILD_PROTOCOL AND CXXMCP_BUILD_SERVER)
    add_test(NAME package_smoke COMMAND
        "${CMAKE_COMMAND}"
        -DREPO_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DBUILD_DIR=${PROJECT_BINARY_DIR}
        -DBUILD_CONFIG=$<CONFIG>
        -P "${PROJECT_SOURCE_DIR}/tests/package_smoke.cmake"
    )
endif()

if(TARGET mcp_protocol AND (TARGET mcp_client OR TARGET mcp_app OR TARGET mcp_gateway OR TARGET mcp_cli))
    add_executable(mcp_test_stdio_child
        fixtures/process_stdio_child.cpp
    )

    target_link_libraries(mcp_test_stdio_child PRIVATE mcp_protocol)
endif()

if(TARGET mcp_client AND TARGET mcp_test_stdio_child)
    add_executable(mcp_process_stdio_transport_tests
        transport/test_process_stdio_transport.cpp
    )

    target_link_libraries(mcp_process_stdio_transport_tests PRIVATE mcp_client)
    target_compile_definitions(mcp_process_stdio_transport_tests PRIVATE
        MCP_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        MCP_TEST_CHILD_EXE="$<TARGET_FILE:mcp_test_stdio_child>"
    )

    add_dependencies(mcp_process_stdio_transport_tests mcp_test_stdio_child)

    add_test(NAME process_stdio_transport COMMAND mcp_process_stdio_transport_tests)
endif()

if(TARGET mcp_app AND TARGET mcp_gateway)
    add_executable(mcp_app_tests
        app/test_app.cpp
    )

    target_link_libraries(mcp_app_tests PRIVATE mcp_app mcp_gateway)

    add_test(NAME app COMMAND mcp_app_tests)
endif()

if(TARGET mcp_app)
    add_executable(mcp_server_management_tests
        app/test_server_management.cpp
    )

    target_link_libraries(mcp_server_management_tests PRIVATE mcp_app)

    add_test(NAME server_management COMMAND mcp_server_management_tests)

    add_executable(mcp_client_discovery_tests
        app/test_client_discovery.cpp
    )

    target_link_libraries(mcp_client_discovery_tests PRIVATE mcp_app)
    if(TARGET mcp_test_stdio_child)
        target_compile_definitions(mcp_client_discovery_tests PRIVATE
            MCP_TEST_CHILD_EXE="$<TARGET_FILE:mcp_test_stdio_child>"
        )
        add_dependencies(mcp_client_discovery_tests mcp_test_stdio_child)
    endif()

    add_test(NAME client_discovery COMMAND mcp_client_discovery_tests)

    add_executable(mcp_tool_management_tests
        app/test_tool_management.cpp
    )

    target_link_libraries(mcp_tool_management_tests PRIVATE mcp_app)

    add_test(NAME tool_management COMMAND mcp_tool_management_tests)

    add_executable(mcp_task_management_tests
        app/test_task_management.cpp
    )

    target_link_libraries(mcp_task_management_tests PRIVATE mcp_app)

    add_test(NAME task_management COMMAND mcp_task_management_tests)
endif()

if(TARGET mcp_gateway AND TARGET mcp_test_stdio_child)
    add_executable(mcp_gateway_tests
        app/test_gateway.cpp
    )

    target_link_libraries(mcp_gateway_tests PRIVATE mcp_gateway)
    target_compile_definitions(mcp_gateway_tests PRIVATE
        MCP_TEST_CHILD_EXE="$<TARGET_FILE:mcp_test_stdio_child>"
    )
    add_dependencies(mcp_gateway_tests mcp_test_stdio_child)

    add_test(NAME gateway COMMAND mcp_gateway_tests)
endif()

if(TARGET mcp_cli AND TARGET mcp_cli_app AND TARGET mcp_test_stdio_child)
    add_executable(mcp_cli_tests
        cli/test_cli.cpp
    )

    target_link_libraries(mcp_cli_tests PRIVATE mcp_cli)
    target_compile_definitions(mcp_cli_tests PRIVATE
        MCP_TEST_CLI_EXE="$<TARGET_FILE:mcp_cli_app>"
        MCP_TEST_CHILD_EXE="$<TARGET_FILE:mcp_test_stdio_child>"
    )
    add_dependencies(mcp_cli_tests mcp_cli_app mcp_test_stdio_child)

    add_test(NAME cli COMMAND mcp_cli_tests)
endif()
