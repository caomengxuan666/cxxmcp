if(NOT DEFINED REPO_SOURCE_DIR)
    message(FATAL_ERROR "REPO_SOURCE_DIR is required")
endif()
if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

set(prefix_dir "${BUILD_DIR}/package-smoke/prefix")
set(consumer_build_dir "${BUILD_DIR}/package-smoke/consumer-build")
set(package_smoke_generator "")
if(DEFINED PACKAGE_SMOKE_GENERATOR)
    set(package_smoke_generator "${PACKAGE_SMOKE_GENERATOR}")
endif()
set(package_smoke_uses_visual_studio OFF)
if(package_smoke_generator MATCHES "Visual Studio")
    set(package_smoke_uses_visual_studio ON)
endif()

file(REMOVE_RECURSE "${prefix_dir}" "${consumer_build_dir}")
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

set(configure_command
    "${CMAKE_COMMAND}"
    -S "${REPO_SOURCE_DIR}/tests/fixtures/package_smoke"
    -B "${consumer_build_dir}"
    "-DCMAKE_PREFIX_PATH=${prefix_dir}"
)
if(NOT package_smoke_generator STREQUAL "")
    list(APPEND configure_command -G "${package_smoke_generator}")
endif()
if(DEFINED PACKAGE_SMOKE_GENERATOR_PLATFORM AND
   NOT PACKAGE_SMOKE_GENERATOR_PLATFORM STREQUAL "")
    list(APPEND configure_command -A "${PACKAGE_SMOKE_GENERATOR_PLATFORM}")
endif()
if(DEFINED PACKAGE_SMOKE_GENERATOR_TOOLSET AND
   NOT PACKAGE_SMOKE_GENERATOR_TOOLSET STREQUAL "")
    list(APPEND configure_command -T "${PACKAGE_SMOKE_GENERATOR_TOOLSET}")
endif()
if(DEFINED PACKAGE_SMOKE_CXX_COMPILER AND
   NOT PACKAGE_SMOKE_CXX_COMPILER STREQUAL "" AND
   NOT package_smoke_uses_visual_studio)
    list(APPEND configure_command
        "-DCMAKE_CXX_COMPILER=${PACKAGE_SMOKE_CXX_COMPILER}")
endif()
if(DEFINED PACKAGE_SMOKE_TOOLCHAIN_FILE AND
   NOT PACKAGE_SMOKE_TOOLCHAIN_FILE STREQUAL "")
    list(APPEND configure_command
        "-DCMAKE_TOOLCHAIN_FILE=${PACKAGE_SMOKE_TOOLCHAIN_FILE}")
endif()
if(DEFINED PACKAGE_SMOKE_VCPKG_TARGET_TRIPLET AND
   NOT PACKAGE_SMOKE_VCPKG_TARGET_TRIPLET STREQUAL "")
    list(APPEND configure_command
        "-DVCPKG_TARGET_TRIPLET=${PACKAGE_SMOKE_VCPKG_TARGET_TRIPLET}")
endif()
if(DEFINED PACKAGE_SMOKE_VCPKG_INSTALLED_DIR AND
   NOT PACKAGE_SMOKE_VCPKG_INSTALLED_DIR STREQUAL "")
    list(APPEND configure_command
        "-DVCPKG_INSTALLED_DIR=${PACKAGE_SMOKE_VCPKG_INSTALLED_DIR}")
endif()
if(NOT package_smoke_uses_visual_studio AND
   DEFINED PACKAGE_SMOKE_BUILD_TYPE AND
   NOT PACKAGE_SMOKE_BUILD_TYPE STREQUAL "")
    list(APPEND configure_command
        "-DCMAKE_BUILD_TYPE=${PACKAGE_SMOKE_BUILD_TYPE}")
endif()
if(DEFINED MSVC_RUNTIME_LIBRARY AND NOT MSVC_RUNTIME_LIBRARY STREQUAL "")
    list(APPEND configure_command
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=${MSVC_RUNTIME_LIBRARY}")
endif()

execute_process(
    COMMAND ${configure_command}
    RESULT_VARIABLE configure_result
)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "package smoke configure failed: ${configure_result}")
endif()

set(build_command "${CMAKE_COMMAND}" --build "${consumer_build_dir}")
if(DEFINED BUILD_CONFIG AND NOT BUILD_CONFIG STREQUAL "")
    list(APPEND build_command --config "${BUILD_CONFIG}")
endif()

execute_process(
    COMMAND ${build_command}
    RESULT_VARIABLE build_result
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "package smoke build failed: ${build_result}")
endif()
