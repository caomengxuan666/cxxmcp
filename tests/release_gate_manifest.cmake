if(NOT DEFINED REPO_SOURCE_DIR)
    message(FATAL_ERROR "REPO_SOURCE_DIR is required")
endif()

set(tests_cmake "${REPO_SOURCE_DIR}/tests/CMakeLists.txt")
if(NOT EXISTS "${tests_cmake}")
    message(FATAL_ERROR "tests/CMakeLists.txt was not found")
endif()

file(READ "${tests_cmake}" tests_cmake_content)

set(required_release_blocking_tests
    protocol
    sdk_boundary
    auth
    transport_contract
    transport_stdio_contract
    client_server
    stdio_transport
    transport_adapters
    http_transport
    rmcp_conformance
    sdk
    public_targets
    package_smoke
    process_stdio_transport
    interop_typescript_client_process_stdio
    interop_python_client_process_stdio
    interop_rmcp_client_process_stdio
)

foreach(test_name IN LISTS required_release_blocking_tests)
    string(REGEX MATCH
        "cxxmcp_mark_release_blocking\\([ \t\r\n]*${test_name}([ \t\r\n]|\\))"
        has_release_gate
        "${tests_cmake_content}")
    if(NOT has_release_gate)
        message(FATAL_ERROR
            "Release-blocking test '${test_name}' is missing cxxmcp_mark_release_blocking()")
    endif()
endforeach()

foreach(header_test
        protocol error config auth transport client server peer handler service sdk)
    string(REGEX MATCH
        "add_cxxmcp_public_header_compile_test\\([ \t\r\n]*${header_test}([ \t\r\n]|\\))"
        has_header_test
        "${tests_cmake_content}")
    if(NOT has_header_test)
        message(FATAL_ERROR
            "Public header compile test '${header_test}' is missing")
    endif()
endforeach()
