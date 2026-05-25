if(NOT DEFINED REPO_SOURCE_DIR)
    message(FATAL_ERROR "REPO_SOURCE_DIR is required")
endif()

set(public_include_roots
    sdk/include
    sdk/core/include
    sdk/protocol/include
    sdk/client/include
    sdk/server/include
    sdk/transport/include
)

set(forbidden_include_patterns
    "#[ \t]*include[ \t]*[<\"]cxxmcp/(app|runtime|gateway|cli)/"
    "#[ \t]*include[ \t]*[<\"]cxxmcp/(profile|policy|discovery)/"
    "#[ \t]*include[ \t]*[<\"]httplib\\.h"
)

set(forbidden_type_patterns
    "\\bGateway[A-Za-z0-9_]*\\b"
    "\\bRuntime[A-Za-z0-9_]*\\b"
    "\\b[A-Za-z0-9_]*Profile\\b"
    "\\bExposure[A-Za-z0-9_]*\\b"
    "\\bTrust[A-Za-z0-9_]*\\b"
    "\\bImport[A-Za-z0-9_]*\\b"
    "\\bExport[A-Za-z0-9_]*\\b"
    "\\bDiscovery[A-Za-z0-9_]*\\b"
    "\\bhttplib::"
)

foreach(root IN LISTS public_include_roots)
    file(GLOB_RECURSE headers
        "${REPO_SOURCE_DIR}/${root}/*.hpp"
    )
    foreach(header IN LISTS headers)
        file(READ "${header}" content)
        file(RELATIVE_PATH relative_header "${REPO_SOURCE_DIR}" "${header}")

        foreach(pattern IN LISTS forbidden_include_patterns)
            if(content MATCHES "${pattern}")
                message(FATAL_ERROR
                    "SDK public header ${relative_header} includes a runtime/gateway boundary header: ${pattern}")
            endif()
        endforeach()

        foreach(pattern IN LISTS forbidden_type_patterns)
            if(content MATCHES "${pattern}")
                message(FATAL_ERROR
                    "SDK public header ${relative_header} exposes a runtime/gateway boundary type: ${pattern}")
            endif()
        endforeach()
    endforeach()
endforeach()
