function(cxxmcp_mark_release_blocking test_name)
    if(TEST "${test_name}")
        set(labels release-blocking ${ARGN})
        set_tests_properties("${test_name}" PROPERTIES LABELS "${labels}")
    endif()
endfunction()

set(CXXMCP_INTEROP_TYPESCRIPT_SDK_VERSION "1.29.0" CACHE STRING
    "Pinned @modelcontextprotocol/sdk version used by interop tests")
set(CXXMCP_INTEROP_PYTHON_MCP_VERSION "1.27.1" CACHE STRING
    "Pinned Python mcp package version used by interop tests")

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

    add_executable(mcp_protocol_serialization_benchmark
        protocol/serialization_benchmark.cpp
    )

    target_link_libraries(mcp_protocol_serialization_benchmark PRIVATE
        mcp_protocol)

    add_test(NAME protocol_serialization_benchmark
        COMMAND mcp_protocol_serialization_benchmark)
    set_tests_properties(protocol_serialization_benchmark PROPERTIES
        LABELS "benchmark;performance;protocol"
        TIMEOUT 30)
endif()

add_test(NAME sdk_boundary COMMAND
    "${CMAKE_COMMAND}"
    "-DREPO_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/tests/sdk_boundary.cmake"
)
cxxmcp_mark_release_blocking(sdk_boundary sdk-boundary)

add_test(NAME release_gate_manifest COMMAND
    "${CMAKE_COMMAND}"
    "-DREPO_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/tests/release_gate_manifest.cmake"
)
cxxmcp_mark_release_blocking(release_gate_manifest release-gate)

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
        protocol_types protocol_types_header.cpp cxxmcp::protocol)
    add_cxxmcp_public_header_compile_test(
        protocol_types_reflect protocol_types_reflect_header.cpp
        cxxmcp::protocol)
    add_cxxmcp_public_header_compile_test(
        error error_header.cpp cxxmcp::protocol)
endif()

if(TARGET mcp_sdk)
    add_cxxmcp_public_header_compile_test(
        config config_header.cpp cxxmcp::sdk)
endif()

if(TARGET mcp_auth)
    add_cxxmcp_public_header_compile_test(
        auth auth_header.cpp cxxmcp::auth)

    add_executable(mcp_auth_tests
        auth/test_auth.cpp
    )

    target_link_libraries(mcp_auth_tests PRIVATE cxxmcp::auth)
    if(TARGET mcp_server)
        target_link_libraries(mcp_auth_tests PRIVATE mcp_server)
        target_compile_definitions(mcp_auth_tests PRIVATE
            CXXMCP_AUTH_TEST_HAS_SERVER=1)
    endif()

    add_test(NAME auth COMMAND mcp_auth_tests)
    cxxmcp_mark_release_blocking(auth auth security)
endif()

if(TARGET mcp_auth_openssl)
    add_executable(mcp_auth_openssl_tests
        auth/test_auth_openssl.cpp
    )

    target_link_libraries(mcp_auth_openssl_tests PRIVATE cxxmcp::auth_openssl)
    if(TARGET mcp_server)
        target_link_libraries(mcp_auth_openssl_tests PRIVATE cxxmcp::server)
    endif()

    add_test(NAME auth_openssl COMMAND mcp_auth_openssl_tests)
    cxxmcp_mark_release_blocking(auth_openssl auth security)
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

    if(MCP_ENABLE_HTTP)
        add_executable(mcp_http_transport_tests
            transport/test_http_transport.cpp
        )

        target_link_libraries(mcp_http_transport_tests PRIVATE
            mcp_client
            mcp_server
        )
        if(CXXMCP_USE_SYSTEM_DEPS)
            target_link_libraries(mcp_http_transport_tests PRIVATE
                "${CXXMCP_HTTPLIB_TARGET}")
        else()
            target_link_libraries(mcp_http_transport_tests PRIVATE cpp_httplib)
        endif()

        add_test(NAME http_transport COMMAND mcp_http_transport_tests)
        cxxmcp_mark_release_blocking(http_transport transport http)

    endif()

    if(MCP_ENABLE_WEBSOCKET)
        add_executable(mcp_websocket_transport_tests
            transport/test_websocket_transport.cpp
        )

        target_link_libraries(mcp_websocket_transport_tests PRIVATE
            mcp_client
            mcp_server
        )
        if(CXXMCP_USE_SYSTEM_DEPS)
            target_link_libraries(mcp_websocket_transport_tests PRIVATE
                "${CXXMCP_HTTPLIB_TARGET}")
        else()
            target_link_libraries(mcp_websocket_transport_tests PRIVATE
                cpp_httplib)
        endif()

        add_test(NAME websocket_transport COMMAND mcp_websocket_transport_tests)
        cxxmcp_mark_release_blocking(websocket_transport transport websocket)

    endif()

    if(MCP_ENABLE_HTTP)

        add_executable(mcp_rmcp_conformance_tests
            interop/test_rmcp_conformance.cpp
        )

        target_link_libraries(mcp_rmcp_conformance_tests PRIVATE
            mcp_client
            mcp_server
        )
        if(CXXMCP_USE_SYSTEM_DEPS)
            target_link_libraries(mcp_rmcp_conformance_tests PRIVATE
                "${CXXMCP_HTTPLIB_TARGET}")
        else()
            target_link_libraries(mcp_rmcp_conformance_tests PRIVATE cpp_httplib)
        endif()
        target_compile_definitions(mcp_rmcp_conformance_tests PRIVATE
            MCP_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        )

        add_test(NAME rmcp_conformance COMMAND mcp_rmcp_conformance_tests)
        cxxmcp_mark_release_blocking(rmcp_conformance interop)
    endif()

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
        "-DPACKAGE_SMOKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "-DPACKAGE_SMOKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
        "-DPACKAGE_SMOKE_VCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}"
        "-DPACKAGE_SMOKE_VCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}"
        "-DPACKAGE_SMOKE_GENERATOR=${CMAKE_GENERATOR}"
        "-DPACKAGE_SMOKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
        "-DPACKAGE_SMOKE_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
        "-DPACKAGE_SMOKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
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

if(TARGET mcp_protocol AND (TARGET mcp_client OR TARGET mcp_server))
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
    set_tests_properties(interop_typescript_client_process_stdio PROPERTIES
        ENVIRONMENT
            "CXXMCP_TYPESCRIPT_SDK_VERSION=${CXXMCP_INTEROP_TYPESCRIPT_SDK_VERSION}")
    cxxmcp_mark_release_blocking(
        interop_typescript_client_process_stdio interop process-stdio)

    add_test(NAME interop_python_client_process_stdio
        COMMAND uv run --with "mcp==${CXXMCP_INTEROP_PYTHON_MCP_VERSION}" python
            "${PROJECT_SOURCE_DIR}/tests/fixtures/process_stdio_client.py"
            "$<TARGET_FILE:mcp_test_stdio_child>"
    )
    cxxmcp_mark_release_blocking(
        interop_python_client_process_stdio interop process-stdio)

    add_test(NAME interop_rmcp_client_process_stdio
        COMMAND "${CMAKE_COMMAND}"
            "-DFIXTURE_SOURCE_DIR=${PROJECT_SOURCE_DIR}/tests/fixtures/rmcp_process_stdio_client"
            "-DRMCP_CRATE_DIR=${PROJECT_SOURCE_DIR}/reference/rmcp/crates/rmcp"
            "-DSERVER_EXE=$<TARGET_FILE:mcp_test_stdio_child>"
            "-DCARGO_TARGET_DIR=${PROJECT_BINARY_DIR}/cargo/rmcp_process_stdio_client"
            "-DCARGO_WORK_DIR=${PROJECT_BINARY_DIR}/cargo/rmcp_process_stdio_client-src"
            -P "${PROJECT_SOURCE_DIR}/tests/run_cargo_fixture.cmake"
    )
    cxxmcp_mark_release_blocking(
        interop_rmcp_client_process_stdio interop process-stdio)
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
