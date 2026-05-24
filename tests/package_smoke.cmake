if(NOT DEFINED REPO_SOURCE_DIR)
    message(FATAL_ERROR "REPO_SOURCE_DIR is required")
endif()
if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

set(prefix_dir "${BUILD_DIR}/package-smoke/prefix")
set(consumer_build_dir "${BUILD_DIR}/package-smoke/consumer-build")

file(MAKE_DIRECTORY "${prefix_dir}")

set(install_command "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${prefix_dir}")
if(DEFINED BUILD_CONFIG AND NOT BUILD_CONFIG STREQUAL "")
    list(APPEND install_command --config "${BUILD_CONFIG}")
endif()

execute_process(
    COMMAND ${install_command}
    RESULT_VARIABLE install_result
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "package smoke install failed: ${install_result}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${REPO_SOURCE_DIR}/tests/fixtures/package_smoke"
        -B "${consumer_build_dir}"
        "-DCMAKE_PREFIX_PATH=${prefix_dir}"
    RESULT_VARIABLE configure_result
)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "package smoke configure failed: ${configure_result}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${consumer_build_dir}"
    RESULT_VARIABLE build_result
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "package smoke build failed: ${build_result}")
endif()
