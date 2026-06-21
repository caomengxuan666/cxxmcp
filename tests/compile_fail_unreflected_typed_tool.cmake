set(source
    "${REPO_SOURCE_DIR}/tests/fixtures/compile_fail/unreflected_typed_tool.cpp")
set(build_dir "${REPO_BINARY_DIR}/compile-fail/unreflected-typed-tool")
file(MAKE_DIRECTORY "${build_dir}")

set(cmake_source_dir "${build_dir}/source")
file(MAKE_DIRECTORY "${cmake_source_dir}")
file(WRITE "${cmake_source_dir}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.23)\n"
    "project(cxxmcp_unreflected_typed_tool_fail LANGUAGES CXX)\n"
    "add_executable(unreflected_typed_tool \"${source}\")\n"
    "target_compile_features(unreflected_typed_tool PRIVATE cxx_std_17)\n"
    "target_include_directories(unreflected_typed_tool PRIVATE\n"
    "  \"${REPO_SOURCE_DIR}/sdk/include\"\n"
    "  \"${REPO_SOURCE_DIR}/sdk/core/include\"\n"
    "  \"${REPO_SOURCE_DIR}/sdk/protocol/include\"\n"
    "  \"${REPO_SOURCE_DIR}/sdk/server/include\"\n"
    "  \"${REPO_SOURCE_DIR}/third_party\"\n"
    ")\n")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${cmake_source_dir}" -B "${build_dir}"
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR
        "compile-fail fixture configure failed unexpectedly:\n"
        "${configure_output}\n${configure_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${build_dir}" --config Debug
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error)
if(build_result EQUAL 0)
    message(FATAL_ERROR
        "unreflected typed tool fixture compiled successfully, but it should "
        "fail with the cxxmcp reflection guidance diagnostic")
endif()

string(CONCAT build_log "${build_output}\n${build_error}")
if(NOT build_log MATCHES
   "cxxmcp typed tool argument/result type is a class or struct but is not reflectable")
    message(FATAL_ERROR
        "unreflected typed tool failed without the expected diagnostic:\n"
        "${build_log}")
endif()
