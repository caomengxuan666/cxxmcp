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
    auth_openssl
    transport_contract
    transport_stdio_contract
    client_server
    stdio_transport
    transport_adapters
    http_transport
    websocket_transport
    rmcp_conformance
    sdk
    public_targets
    package_smoke
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
        protocol protocol_types protocol_types_reflect error config auth transport
        auth_client_orchestrator auth_constant_time auth_dpop
        auth_http_jwks_endpoint auth_http_metadata_endpoint
        auth_http_token_endpoint auth_jwks auth_lifecycle
        auth_loopback_receiver auth_metadata auth_pkce auth_registration
        auth_server_auth_endpoints auth_server_auth_provider auth_token
        auth_types auth_www_auth auth_openssl_base64url auth_openssl_dpop
        auth_openssl_jwk auth_openssl_jws auth_openssl_jws_verify
        auth_openssl_jwt auth_openssl_pkce
        auth_openssl_server_auth_provider auth_openssl_sha256
        websocket_transport client server peer handler service sdk)
    string(REGEX MATCH
        "add_cxxmcp_public_header_compile_test\\([ \t\r\n]*${header_test}([ \t\r\n]|\\))"
        has_header_test
        "${tests_cmake_content}")
    if(NOT has_header_test)
        message(FATAL_ERROR
            "Public header compile test '${header_test}' is missing")
    endif()
endforeach()
