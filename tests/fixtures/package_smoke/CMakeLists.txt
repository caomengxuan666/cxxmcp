cmake_minimum_required(VERSION 3.23)
project(cxxmcp_package_smoke_consumer LANGUAGES CXX)

find_package(cxxmcp CONFIG REQUIRED)

function(assert_sdk_target_has_no_runtime_or_gateway_dependency target_name)
    foreach(property_name
            INTERFACE_LINK_LIBRARIES
            INTERFACE_INCLUDE_DIRECTORIES)
        get_target_property(property_values "${target_name}" "${property_name}")
        if(NOT property_values)
            continue()
        endif()
        foreach(property_value IN LISTS property_values)
            if(property_value STREQUAL "cxxmcp::runtime" OR
               property_value STREQUAL "cxxmcp::gateway" OR
               property_value STREQUAL "cxxmcp::cli" OR
               property_value STREQUAL "mcp_app" OR
               property_value STREQUAL "mcp_gateway" OR
               property_value STREQUAL "mcp_cli" OR
               property_value MATCHES "[/\\\\]runtime[/\\\\]" OR
               property_value MATCHES "[/\\\\]tools[/\\\\]")
                message(FATAL_ERROR
                    "${target_name} ${property_name} leaks optional tooling "
                    "dependency: ${property_value}")
            endif()
        endforeach()
    endforeach()
endfunction()

foreach(sdk_target
        cxxmcp::protocol
        cxxmcp::client
        cxxmcp::server
        cxxmcp::transport
        cxxmcp::peer
        cxxmcp::handler
        cxxmcp::service
        cxxmcp::sdk)
    assert_sdk_target_has_no_runtime_or_gateway_dependency("${sdk_target}")
endforeach()

function(add_cxxmcp_package_smoke_target target_name source_file package_target)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE ${package_target})
endfunction()

add_cxxmcp_package_smoke_target(
    cxxmcp_package_protocol protocol.cpp cxxmcp::protocol)
add_cxxmcp_package_smoke_target(
    cxxmcp_package_client client.cpp cxxmcp::client)
add_cxxmcp_package_smoke_target(
    cxxmcp_package_server server.cpp cxxmcp::server)
add_cxxmcp_package_smoke_target(
    cxxmcp_package_transport transport.cpp cxxmcp::transport)
add_cxxmcp_package_smoke_target(
    cxxmcp_package_peer peer.cpp cxxmcp::peer)
add_cxxmcp_package_smoke_target(
    cxxmcp_package_handler handler.cpp cxxmcp::handler)
add_cxxmcp_package_smoke_target(
    cxxmcp_package_service service.cpp cxxmcp::service)
add_cxxmcp_package_smoke_target(
    cxxmcp_package_sdk sdk.cpp cxxmcp::sdk)
add_cxxmcp_package_smoke_target(
    cxxmcp_package_request request.cpp cxxmcp::sdk)
add_cxxmcp_package_smoke_target(
    cxxmcp_package_plugin_sdk plugin_sdk.cpp cxxmcp::plugin_sdk)
add_cxxmcp_package_smoke_target(
    cxxmcp_package_adapters adapters.cpp cxxmcp::adapters)
