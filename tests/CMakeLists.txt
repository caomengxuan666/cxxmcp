function(cxxmcp_mark_release_blocking test_name)
    if(TEST "${test_name}")
        set(labels release-blocking ${ARGN})
        set_tests_properties("${test_name}" PROPERTIES LABELS "${labels}")
    endif()
endfunction()

if(TARGET mcp_protocol)
    add_executable(mcp_protocol_tests
        protocol/test_protocol.cpp
    )

    target_link_libraries(mcp_protocol_tests PRIVATE mcp_protocol)

    target_compile_definitions(mcp_protocol_tests PRIVATE
        MCP_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    )

    add_test(NAME protocol COMMAND mcp_protocol_tests)
    cxxmcp_mark_release_blocking(protocol protocol)
endif()

add_test(NAME sdk_boundary COMMAND
    "${CMAKE_COMMAND}"
    "-DREPO_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/tests/sdk_boundary.cmake"
)
cxxmcp_mark_release_blocking(sdk_boundary sdk-boundary)

function(add_cxxmcp_public_header_compile_test test_name source_file package_target)
    add_executable("mcp_public_header_${test_name}"
        "sdk/public_headers/${source_file}"
    )

    target_link_libraries("mcp_public_header_${test_name}" PRIVATE
        "${package_target}"
    )

    set_target_properties("mcp_public_header_${test_name}" PROPERTIES
        CXXMCP_PUBLIC_HEADER_TEST TRUE
    )

    add_test(NAME "public_header_${test_name}"
        COMMAND "mcp_public_header_${test_name}"
    )
    cxxmcp_mark_release_blocking(
        "public_header_${test_name}" public-api)
endfunction()

if(TARGET mcp_protocol)
    add_cxxmcp_public_header_compile_test(
        protocol protocol_header.cpp cxxmcp::protocol)
    add_cxxmcp_public_header_compile_test(
        error error_header.cpp cxxmcp::protocol)
endif()

if(TARGET mcp_sdk)
    add_cxxmcp_public_header_compile_test(
        config config_header.cpp cxxmcp::sdk)
endif()

if(TARGET mcp_transport)
    add_executable(mcp_transport_contract_tests
        transport/test_transport_contract.cpp
    )

    target_link_libraries(mcp_transport_contract_tests PRIVATE mcp_transport)

    add_test(NAME transport_contract COMMAND mcp_transport_contract_tests)
    cxxmcp_mark_release_blocking(transport_contract transport)

    add_executable(mcp_transport_stdio_contract_tests
        transport/test_transport_stdio_contract.cpp
    )

    target_link_libraries(mcp_transport_stdio_contract_tests PRIVATE mcp_transport)

    add_test(NAME transport_stdio_contract COMMAND mcp_transport_stdio_contract_tests)
    cxxmcp_mark_release_blocking(transport_stdio_contract transport)

    add_cxxmcp_public_header_compile_test(
        transport transport_header.cpp cxxmcp::transport)
endif()

if(TARGET mcp_client AND TARGET mcp_server)
    add_cxxmcp_public_header_compile_test(
        client client_header.cpp cxxmcp::client)
    add_cxxmcp_public_header_compile_test(
        server server_header.cpp cxxmcp::server)
    add_cxxmcp_public_header_compile_test(
        peer peer_header.cpp cxxmcp::peer)
    add_cxxmcp_public_header_compile_test(
        handler handler_header.cpp cxxmcp::handler)
    add_cxxmcp_public_header_compile_test(
        service service_header.cpp cxxmcp::service)
    add_cxxmcp_public_header_compile_test(
        sdk sdk_header.cpp cxxmcp::sdk)

    add_executable(mcp_client_server_tests
        client_server/test_client_server.cpp
    )

    target_link_libraries(mcp_client_server_tests PRIVATE mcp_client mcp_server)

    add_test(NAME client_server COMMAND mcp_client_server_tests)
    cxxmcp_mark_release_blocking(client_server peer-service)

    add_executable(mcp_stdio_transport_tests
        transport/test_stdio_transport.cpp
    )

    target_link_libraries(mcp_stdio_transport_tests PRIVATE mcp_client mcp_server)

    add_test(NAME stdio_transport COMMAND mcp_stdio_transport_tests)
    cxxmcp_mark_release_blocking(stdio_transport transport)

    add_executable(mcp_transport_adapter_tests
        transport/test_transport_adapters.cpp
    )

    target_link_libraries(mcp_transport_adapter_tests PRIVATE mcp_client mcp_server)

    add_test(NAME transport_adapters COMMAND mcp_transport_adapter_tests)
    cxxmcp_mark_release_blocking(transport_adapters transport)

    add_executable(mcp_http_transport_tests
        transport/test_http_transport.cpp
    )

    target_link_libraries(mcp_http_transport_tests PRIVATE
        mcp_client
        mcp_server
        cpp_httplib
    )

    add_test(NAME http_transport COMMAND mcp_http_transport_tests)
    cxxmcp_mark_release_blocking(http_transport transport http)

    add_executable(mcp_rmcp_conformance_tests
        interop/test_rmcp_conformance.cpp
    )

    target_link_libraries(mcp_rmcp_conformance_tests PRIVATE
        mcp_client
        mcp_server
        cpp_httplib
    )
    target_compile_definitions(mcp_rmcp_conformance_tests PRIVATE
        MCP_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    )

    add_test(NAME rmcp_conformance COMMAND mcp_rmcp_conformance_tests)
    cxxmcp_mark_release_blocking(rmcp_conformance interop)

    add_executable(mcp_sdk_tests
        sdk/test_sdk.cpp
    )

    target_link_libraries(mcp_sdk_tests PRIVATE cxxmcp::sdk)

    add_test(NAME sdk COMMAND mcp_sdk_tests)
    cxxmcp_mark_release_blocking(sdk sdk)

    add_executable(mcp_public_target_tests
        sdk/test_public_targets.cpp
    )

    target_link_libraries(mcp_public_target_tests PRIVATE
        cxxmcp::handler
        cxxmcp::service
    )

    add_test(NAME public_targets COMMAND mcp_public_target_tests)
    cxxmcp_mark_release_blocking(public_targets public-api package)
endif()

if(CXXMCP_BUILD_TESTS AND TARGET mcp_sdk AND TARGET mcp_protocol AND TARGET mcp_server)
    set(package_smoke_args
        -DREPO_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DBUILD_DIR=${PROJECT_BINARY_DIR}
        -DBUILD_CONFIG=$<CONFIG>
    )
    if(MSVC)
        set(package_smoke_msvc_runtime_library "")
        if(CXXMCP_MSVC_RUNTIME_LIBRARY)
            set(package_smoke_msvc_runtime_library
                "${CXXMCP_MSVC_RUNTIME_LIBRARY}")
        elseif(DEFINED CMAKE_MSVC_RUNTIME_LIBRARY AND
               NOT CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "")
            set(package_smoke_msvc_runtime_library
                "${CMAKE_MSVC_RUNTIME_LIBRARY}")
        endif()

        if(package_smoke_msvc_runtime_library)
            list(APPEND package_smoke_args
                "-DMSVC_RUNTIME_LIBRARY=${package_smoke_msvc_runtime_library}")
        endif()
    endif()

    add_test(NAME package_smoke COMMAND
        "${CMAKE_COMMAND}"
        ${package_smoke_args}
        -P "${PROJECT_SOURCE_DIR}/tests/package_smoke.cmake"
    )
    cxxmcp_mark_release_blocking(package_smoke package)
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
    cxxmcp_mark_release_blocking(process_stdio_transport transport process-stdio)

    add_test(NAME interop_typescript_client_process_stdio
        COMMAND node
            "${PROJECT_SOURCE_DIR}/tests/fixtures/process_stdio_client_node.mjs"
            "$<TARGET_FILE:mcp_test_stdio_child>"
    )
    cxxmcp_mark_release_blocking(
        interop_typescript_client_process_stdio interop process-stdio)

    add_test(NAME interop_python_client_process_stdio
        COMMAND uv run --with mcp python
            "${PROJECT_SOURCE_DIR}/tests/fixtures/process_stdio_client.py"
            "$<TARGET_FILE:mcp_test_stdio_child>"
    )
    cxxmcp_mark_release_blocking(
        interop_python_client_process_stdio interop process-stdio)
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

    target_link_libraries(mcp_client_discovery_tests PRIVATE
        mcp_app
        cpp_httplib
    )
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

    target_link_libraries(mcp_gateway_tests PRIVATE
        mcp_gateway
        cpp_httplib
    )
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

    target_link_libraries(mcp_cli_tests PRIVATE
        mcp_cli
        cpp_httplib
    )
    target_compile_definitions(mcp_cli_tests PRIVATE
        MCP_TEST_CLI_EXE="$<TARGET_FILE:mcp_cli_app>"
        MCP_TEST_CHILD_EXE="$<TARGET_FILE:mcp_test_stdio_child>"
    )
    add_dependencies(mcp_cli_tests mcp_cli_app mcp_test_stdio_child)

    add_test(NAME cli COMMAND mcp_cli_tests)
endif()

get_property(cxxmcp_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(cxxmcp_test_target IN LISTS cxxmcp_test_targets)
    get_target_property(cxxmcp_public_header_test
        "${cxxmcp_test_target}" CXXMCP_PUBLIC_HEADER_TEST)
    if(cxxmcp_public_header_test)
        cxxmcp_require_sdk_cxx("${cxxmcp_test_target}")
    else()
        cxxmcp_require_cxx20("${cxxmcp_test_target}")
    endif()
endforeach()
