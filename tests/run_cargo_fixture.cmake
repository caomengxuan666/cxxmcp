if(NOT DEFINED FIXTURE_SOURCE_DIR)
    message(FATAL_ERROR "FIXTURE_SOURCE_DIR is required")
endif()

if(NOT DEFINED RMCP_CRATE_DIR)
    message(FATAL_ERROR "RMCP_CRATE_DIR is required")
endif()

if(NOT DEFINED SERVER_EXE)
    message(FATAL_ERROR "SERVER_EXE is required")
endif()

if(NOT DEFINED CARGO_TARGET_DIR)
    message(FATAL_ERROR "CARGO_TARGET_DIR is required")
endif()

if(NOT DEFINED CARGO_WORK_DIR)
    message(FATAL_ERROR "CARGO_WORK_DIR is required")
endif()

file(TO_CMAKE_PATH "${RMCP_CRATE_DIR}" cxxmcp_rmcp_crate_dir)
file(MAKE_DIRECTORY "${CARGO_WORK_DIR}/src")
configure_file(
    "${FIXTURE_SOURCE_DIR}/src/main.rs"
    "${CARGO_WORK_DIR}/src/main.rs"
    COPYONLY
)
file(WRITE "${CARGO_WORK_DIR}/Cargo.toml"
"[package]
name = \"rmcp_process_stdio_client\"
version = \"0.1.0\"
edition = \"2024\"

[dependencies]
anyhow = \"1\"
rmcp = { path = \"${cxxmcp_rmcp_crate_dir}\", default-features = false, features = [\"client\", \"transport-child-process\"] }
serde_json = \"1\"
tokio = { version = \"1\", features = [\"macros\", \"rt-multi-thread\", \"process\"] }
"
)

set(ENV{CARGO_TARGET_DIR} "${CARGO_TARGET_DIR}")

if(DEFINED ENV{CXXMCP_CARGO_PROXY} AND NOT "$ENV{CXXMCP_CARGO_PROXY}" STREQUAL "")
    set(ENV{CARGO_HTTP_PROXY} "$ENV{CXXMCP_CARGO_PROXY}")
    set(ENV{CARGO_HTTPS_PROXY} "$ENV{CXXMCP_CARGO_PROXY}")
    set(ENV{HTTP_PROXY} "$ENV{CXXMCP_CARGO_PROXY}")
    set(ENV{HTTPS_PROXY} "$ENV{CXXMCP_CARGO_PROXY}")
    set(ENV{ALL_PROXY} "$ENV{CXXMCP_CARGO_PROXY}")
endif()

set(cxxmcp_cargo_args run)
if(DEFINED ENV{CXXMCP_RUST_FIXTURE_OFFLINE} AND "$ENV{CXXMCP_RUST_FIXTURE_OFFLINE}" STREQUAL "1")
    list(APPEND cxxmcp_cargo_args --offline)
endif()
list(APPEND cxxmcp_cargo_args --manifest-path "${CARGO_WORK_DIR}/Cargo.toml" -- "${SERVER_EXE}")

execute_process(
    COMMAND cargo ${cxxmcp_cargo_args}
    RESULT_VARIABLE cxxmcp_cargo_result
)

if(NOT cxxmcp_cargo_result EQUAL 0)
    message(FATAL_ERROR "cargo fixture failed with exit code ${cxxmcp_cargo_result}")
endif()
