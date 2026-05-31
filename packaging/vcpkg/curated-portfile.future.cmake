# Future curated-registry portfile sketch.
#
# This file is not consumed by the overlay port. It documents the shape expected
# for a future microsoft/vcpkg curated-registry PR after cxxmcp has enough
# maturity evidence and a release tag with a known source archive hash.

vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO caomengxuan666/cxxmcp
    REF v@CXXMCP_VERSION@
    SHA512 @CXXMCP_RELEASE_ARCHIVE_SHA512@
    HEAD_REF master
)

set(CXXMCP_VCPKG_ENABLE_HTTP OFF)
if("http" IN_LIST FEATURES OR
   "http-openssl" IN_LIST FEATURES OR
   "websocket" IN_LIST FEATURES)
    set(CXXMCP_VCPKG_ENABLE_HTTP ON)
endif()

set(CXXMCP_VCPKG_ENABLE_WEBSOCKET OFF)
if("websocket" IN_LIST FEATURES)
    set(CXXMCP_VCPKG_ENABLE_WEBSOCKET ON)
endif()

set(CXXMCP_VCPKG_ENABLE_AUTH OFF)
if("auth" IN_LIST FEATURES)
    set(CXXMCP_VCPKG_ENABLE_AUTH ON)
endif()

set(CXXMCP_VCPKG_AUTH_CRYPTO NONE)
if("openssl" IN_LIST FEATURES)
    set(CXXMCP_VCPKG_AUTH_CRYPTO OpenSSL)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DCXXMCP_BUILD_SDK=ON
        -DCXXMCP_BUILD_EXAMPLES=OFF
        -DCXXMCP_BUILD_TESTS=OFF
        -DCXXMCP_BUILD_DOCS=OFF
        -DCXXMCP_ENABLE_HTTP=${CXXMCP_VCPKG_ENABLE_HTTP}
        -DCXXMCP_ENABLE_WEBSOCKET=${CXXMCP_VCPKG_ENABLE_WEBSOCKET}
        -DCXXMCP_ENABLE_AUTH=${CXXMCP_VCPKG_ENABLE_AUTH}
        -DCXXMCP_AUTH_CRYPTO=${CXXMCP_VCPKG_AUTH_CRYPTO}
        -DCXXMCP_USE_SYSTEM_DEPS=ON
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME cxxmcp CONFIG_PATH lib/cmake/cxxmcp)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
