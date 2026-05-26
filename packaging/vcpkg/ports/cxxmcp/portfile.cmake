get_filename_component(SOURCE_PATH "${CURRENT_PORT_DIR}/../../../.." ABSOLUTE)

vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DCXXMCP_BUILD_SDK=ON
        -DCXXMCP_BUILD_RUNTIME=OFF
        -DCXXMCP_BUILD_APP=OFF
        -DCXXMCP_BUILD_GATEWAY=OFF
        -DCXXMCP_BUILD_CLI=OFF
        -DCXXMCP_BUILD_EXAMPLES=OFF
        -DCXXMCP_BUILD_TESTS=OFF
        -DCXXMCP_BUILD_DOCS=OFF
        -DCXXMCP_USE_SYSTEM_DEPS=ON
        -DBUILD_SHARED_LIBS=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME cxxmcp CONFIG_PATH lib/cmake/cxxmcp)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
