if(NOT TARGET mcp_client AND NOT TARGET mcp_server)
    return()
endif()

function(cxxmcp_example target_name)
    target_compile_features("${target_name}" PRIVATE cxx_std_20)
endfunction()

function(cxxmcp_example_smoke_test test_name target_name)
    if(BUILD_TESTING)
        add_test(NAME "${test_name}" COMMAND "${target_name}")
        set_tests_properties("${test_name}" PROPERTIES
                             LABELS examples
                             TIMEOUT 30)
    endif()
endfunction()

if(TARGET mcp_sdk)
    add_executable(mcp_example_cxx17_consumer cxx17_consumer.cpp)
    target_link_libraries(mcp_example_cxx17_consumer PRIVATE mcp_sdk)
    target_compile_features(mcp_example_cxx17_consumer PRIVATE cxx_std_17)
    cxxmcp_example_smoke_test(example_cxx17_consumer
                              mcp_example_cxx17_consumer)
endif()

if(TARGET mcp_server)
    add_executable(mcp_example_stdio_server stdio_server.cpp)
    target_link_libraries(mcp_example_stdio_server PRIVATE mcp_server)
    cxxmcp_example(mcp_example_stdio_server)

    add_executable(mcp_example_typed_stdio_server typed_stdio_server.cpp)
    target_link_libraries(mcp_example_typed_stdio_server PRIVATE mcp_server)
    cxxmcp_example(mcp_example_typed_stdio_server)
endif()

if(TARGET mcp_client AND TARGET mcp_server)
    add_executable(mcp_example_auth_bearer_http auth_bearer_http.cpp)
    target_link_libraries(mcp_example_auth_bearer_http PRIVATE mcp_client
                                                               mcp_server)
    cxxmcp_example(mcp_example_auth_bearer_http)
    cxxmcp_example_smoke_test(example_auth_bearer_http
                              mcp_example_auth_bearer_http)

    add_executable(mcp_example_server_stdio_peer server_stdio_peer.cpp)
    target_link_libraries(mcp_example_server_stdio_peer PRIVATE mcp_client
                                                                mcp_server)
    cxxmcp_example(mcp_example_server_stdio_peer)

    add_executable(mcp_example_server_peer server_peer.cpp)
    target_link_libraries(mcp_example_server_peer PRIVATE mcp_client
                                                          mcp_server)
    cxxmcp_example(mcp_example_server_peer)
    cxxmcp_example_smoke_test(example_server_peer mcp_example_server_peer)

    add_executable(mcp_example_handler_contracts handler_contracts.cpp)
    target_link_libraries(mcp_example_handler_contracts PRIVATE mcp_client
                                                                mcp_server)
    cxxmcp_example(mcp_example_handler_contracts)
    cxxmcp_example_smoke_test(example_handler_contracts
                              mcp_example_handler_contracts)
endif()

if(TARGET mcp_client AND TARGET mcp_server)
    add_executable(mcp_example_client_peer client_peer.cpp)
    target_link_libraries(mcp_example_client_peer PRIVATE mcp_client
                                                          mcp_server)
    cxxmcp_example(mcp_example_client_peer)
    cxxmcp_example_smoke_test(example_client_peer mcp_example_client_peer)

    add_executable(mcp_example_timeout_cancellation
                   timeout_cancellation.cpp)
    target_link_libraries(mcp_example_timeout_cancellation PRIVATE mcp_client
                                                                  mcp_server)
    cxxmcp_example(mcp_example_timeout_cancellation)
    cxxmcp_example_smoke_test(example_timeout_cancellation
                              mcp_example_timeout_cancellation)

    add_executable(mcp_example_elicitation_client elicitation_client.cpp)
    target_link_libraries(mcp_example_elicitation_client PRIVATE mcp_client
                                                                 mcp_server)
    cxxmcp_example(mcp_example_elicitation_client)
    cxxmcp_example_smoke_test(example_elicitation_client
                              mcp_example_elicitation_client)
endif()

if(TARGET mcp_client AND TARGET mcp_server)
    add_executable(mcp_example_client_loopback client_loopback.cpp)
    target_link_libraries(mcp_example_client_loopback PRIVATE mcp_client
                                                          mcp_server)
    cxxmcp_example(mcp_example_client_loopback)
    cxxmcp_example_smoke_test(example_client_loopback
                              mcp_example_client_loopback)

    add_executable(mcp_example_task_async_client_server
                   task_async_client_server.cpp)
    target_link_libraries(mcp_example_task_async_client_server
                          PRIVATE mcp_client mcp_server)
    cxxmcp_example(mcp_example_task_async_client_server)
    cxxmcp_example_smoke_test(example_task_async_client_server
                              mcp_example_task_async_client_server)
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
    cxxmcp_example_smoke_test(example_process_stdio_client
                              mcp_example_process_stdio_client)
endif()

if(TARGET mcp_client AND TARGET mcp_server)
    add_executable(mcp_example_streamable_http_client
                   streamable_http_client.cpp)
    target_link_libraries(mcp_example_streamable_http_client PRIVATE
                          mcp_client mcp_server)
    cxxmcp_example(mcp_example_streamable_http_client)
endif()

if(TARGET mcp_auth_openssl AND TARGET mcp_client AND TARGET mcp_server)
    add_executable(mcp_example_auth_dpop_openssl auth_dpop_openssl.cpp)
    target_link_libraries(mcp_example_auth_dpop_openssl PRIVATE
                          mcp_auth_openssl mcp_client mcp_server)
    cxxmcp_example(mcp_example_auth_dpop_openssl)
    cxxmcp_example_smoke_test(example_auth_dpop_openssl
                              mcp_example_auth_dpop_openssl)

endif()
