cmake_minimum_required(VERSION 3.23)

project(MCPServerCpp
    VERSION 2.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries by default" FORCE)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING
        "Use the static MSVC runtime" FORCE)
    set(wxBUILD_USE_STATIC_RUNTIME ON CACHE BOOL "Link using the static runtime library" FORCE)
endif()

add_library(mcp_core INTERFACE)
target_include_directories(mcp_core INTERFACE core/include)

add_library(jsonrpcpp INTERFACE)
target_include_directories(jsonrpcpp INTERFACE
    third_party/jsonrpcpp
    third_party
)

add_library(mcp_protocol STATIC)
target_sources(mcp_protocol PRIVATE protocol/src/serialization.cpp)
target_include_directories(mcp_protocol PUBLIC
    protocol/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(mcp_protocol PUBLIC mcp_core jsonrpcpp)

add_library(mcp_client STATIC
    client/src/client.cpp
    client/src/stdio_transport.cpp
)
target_include_directories(mcp_client PUBLIC client/include)
target_link_libraries(mcp_client PUBLIC mcp_core mcp_protocol)

add_library(mcp_server STATIC
    server/src/registry.cpp
    server/src/server.cpp
    server/src/stdio_transport.cpp
)
target_include_directories(mcp_server PUBLIC server/include)
target_link_libraries(mcp_server PUBLIC mcp_core mcp_protocol)

add_library(mcp_plugin_sdk INTERFACE)
target_include_directories(mcp_plugin_sdk INTERFACE plugin-sdk/include)
target_link_libraries(mcp_plugin_sdk INTERFACE mcp_core mcp_protocol)

add_library(mcp_adapters INTERFACE)
target_include_directories(mcp_adapters INTERFACE adapters/include)
target_link_libraries(mcp_adapters INTERFACE mcp_core mcp_protocol mcp_server mcp_plugin_sdk)

add_library(mcp_observability INTERFACE)
target_include_directories(mcp_observability INTERFACE observability/include)

set(wxBUILD_SHARED OFF CACHE BOOL "Build wx libraries as shared libs" FORCE)
set(wxBUILD_MONOLITHIC OFF CACHE BOOL "Build wxWidgets as separate static libraries" FORCE)
set(wxBUILD_SAMPLES OFF CACHE STRING "Disable wxWidgets samples" FORCE)
set(wxBUILD_TESTS OFF CACHE STRING "Disable wxWidgets tests" FORCE)
set(wxBUILD_DEMOS OFF CACHE BOOL "Disable wxWidgets demos" FORCE)
set(wxBUILD_BENCHMARKS OFF CACHE BOOL "Disable wxWidgets benchmarks" FORCE)
set(wxBUILD_INSTALL OFF CACHE BOOL "Disable wxWidgets install targets" FORCE)
set(wxBUILD_LOCALES OFF CACHE STRING "Disable wxWidgets locales" FORCE)
set(wxUSE_HTML OFF CACHE BOOL "Disable wxHTML" FORCE)
set(wxUSE_AUI OFF CACHE BOOL "Disable wxAUI" FORCE)
set(wxUSE_RIBBON OFF CACHE BOOL "Disable wxRibbon" FORCE)
set(wxUSE_STC OFF CACHE BOOL "Disable wxStyledTextCtrl" FORCE)
set(wxUSE_MEDIACTRL OFF CACHE BOOL "Disable wxMediaCtrl" FORCE)
set(wxUSE_WEBVIEW ON CACHE BOOL "Enable wxWebView" FORCE)
set(wxUSE_WEBVIEW_EDGE ON CACHE BOOL "Enable wxWebView Edge backend" FORCE)
set(wxUSE_OPENGL OFF CACHE BOOL "Disable wxOpenGL" FORCE)
set(wxUSE_HELP OFF CACHE BOOL "Disable wxHelp" FORCE)
set(wxUSE_WXHTML_HELP OFF CACHE BOOL "Disable wxHTML help" FORCE)
set(wxUSE_MS_HTML_HELP OFF CACHE BOOL "Disable MS HTML help" FORCE)
set(wxUSE_ARTPROVIDER_TANGO OFF CACHE BOOL "Disable Tango art provider" FORCE)
set(wxUSE_RICHTEXT OFF CACHE BOOL "Disable wxRichTextCtrl" FORCE)
set(wxUSE_SVG OFF CACHE BOOL "Disable wxSVG" FORCE)
set(wxUSE_REGEX OFF CACHE STRING "Disable wxRegEx" FORCE)
set(wxUSE_ZLIB OFF CACHE STRING "Disable wxZlib" FORCE)
set(wxUSE_EXPAT OFF CACHE STRING "Disable wxExpat" FORCE)
set(wxUSE_LIBJPEG OFF CACHE STRING "Disable wxJPEG" FORCE)
set(wxUSE_LIBPNG OFF CACHE STRING "Disable wxPNG" FORCE)
set(wxUSE_LIBTIFF OFF CACHE STRING "Disable wxTIFF" FORCE)
set(wxUSE_LIBWEBP OFF CACHE STRING "Disable wxWebP" FORCE)
set(wxUSE_NANOSVG OFF CACHE STRING "Disable wxNanoSVG" FORCE)
set(wxUSE_LUNASVG OFF CACHE STRING "Disable wxLunaSVG" FORCE)

add_subdirectory(third_party/wxWidgets EXCLUDE_FROM_ALL)

add_library(mcp_app STATIC
    app/src/serialization.cpp
    app/src/services.cpp
    app/src/tool_management.cpp
)
target_include_directories(mcp_app PUBLIC app/include)
target_link_libraries(mcp_app PUBLIC mcp_core mcp_protocol)

add_library(mcp_cli STATIC
    cli/src/commands.cpp
)
target_include_directories(mcp_cli PUBLIC cli/include)
target_link_libraries(mcp_cli PUBLIC mcp_core mcp_app mcp_client mcp_server mcp_plugin_sdk)

add_library(mcp_gui STATIC
    gui/src/app.cpp
    gui/src/controller.cpp
    gui/src/dashboard.cpp
)
target_include_directories(mcp_gui PUBLIC gui/include)
target_link_libraries(mcp_gui PUBLIC mcp_core mcp_app mcp_client wx::core wxwebview)

add_executable(mcp_gui_app
    gui/src/main.cpp
)
target_link_libraries(mcp_gui_app PRIVATE mcp_gui)

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
