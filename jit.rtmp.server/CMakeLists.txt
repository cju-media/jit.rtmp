# NOTE: unlike the two "_tilde" externals, this object has no MSP signal I/O at
# all (it's a pure subprocess manager), so its Max object name matches this
# folder name exactly - no "_tilde" -> "~" suffix rewriting applies here.

set(MIN_API_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../min-api)
set(C74_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/../externals")
include(${MIN_API_DIR}/script/min-pretarget.cmake)

set(SOURCE_FILES
    source/jit.rtmp.server.cpp
)

add_library(${PROJECT_NAME} MODULE ${SOURCE_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${MIN_API_DIR}/include
    ${C74_INCLUDES}
)

# -----------------------------------------------------------------------
# Vendor a real MediaMTX (https://github.com/bluenviron/mediamtx, MIT license)
# server binary + its default config, bundled straight into this external's
# own .mxo (Contents/Resources) at build time, so a distributed package needs
# no separate server install - see README. Downloaded/verified once per build
# tree rather than committed to this git repo as a binary blob.
# -----------------------------------------------------------------------
if(APPLE)
    set(MEDIAMTX_VERSION "v1.20.0")

    if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
        set(MEDIAMTX_PLATFORM "darwin_arm64")
        set(MEDIAMTX_SHA256 "5923ea2517543f21d4c182f9ad832809f71393cf63409350556f372b8013f440")
    else()
        set(MEDIAMTX_PLATFORM "darwin_amd64")
        set(MEDIAMTX_SHA256 "811c33593a74b11397d60787c95cb0550780aac7f9879501ef10be49bcf0f800")
    endif()

    set(MEDIAMTX_ARCHIVE_NAME "mediamtx_${MEDIAMTX_VERSION}_${MEDIAMTX_PLATFORM}.tar.gz")
    set(MEDIAMTX_URL "https://github.com/bluenviron/mediamtx/releases/download/${MEDIAMTX_VERSION}/${MEDIAMTX_ARCHIVE_NAME}")
    set(MEDIAMTX_DIST_DIR "${CMAKE_CURRENT_BINARY_DIR}/mediamtx_dist")
    set(MEDIAMTX_BINARY "${MEDIAMTX_DIST_DIR}/mediamtx")

    if(NOT EXISTS "${MEDIAMTX_BINARY}")
        set(MEDIAMTX_ARCHIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${MEDIAMTX_ARCHIVE_NAME}")
        message(STATUS "Downloading MediaMTX ${MEDIAMTX_VERSION} (${MEDIAMTX_PLATFORM})...")
        file(DOWNLOAD "${MEDIAMTX_URL}" "${MEDIAMTX_ARCHIVE_PATH}"
            EXPECTED_HASH SHA256=${MEDIAMTX_SHA256}
            STATUS MEDIAMTX_DOWNLOAD_STATUS
        )
        list(GET MEDIAMTX_DOWNLOAD_STATUS 0 MEDIAMTX_DOWNLOAD_CODE)
        if(NOT MEDIAMTX_DOWNLOAD_CODE EQUAL 0)
            list(GET MEDIAMTX_DOWNLOAD_STATUS 1 MEDIAMTX_DOWNLOAD_MSG)
            message(WARNING
                "Could not download MediaMTX (${MEDIAMTX_DOWNLOAD_MSG}) - jit.rtmp.server will build "
                "without a bundled server binary. Set its @mediamtx_path attribute to an existing "
                "mediamtx install to use it anyway, or fix connectivity and reconfigure to bundle it."
            )
            file(REMOVE "${MEDIAMTX_ARCHIVE_PATH}")
        else()
            file(MAKE_DIRECTORY "${MEDIAMTX_DIST_DIR}")
            file(ARCHIVE_EXTRACT
                INPUT "${MEDIAMTX_ARCHIVE_PATH}"
                DESTINATION "${MEDIAMTX_DIST_DIR}"
            )
        endif()
    endif()

    if(EXISTS "${MEDIAMTX_BINARY}")
        # Registered before min-posttarget.cmake's own POST_BUILD steps (PkgInfo
        # copy, ad-hoc codesign) below, so this runs first and the final codesign
        # --deep covers the bundled mediamtx binary too.
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${MEDIAMTX_DIST_DIR}/mediamtx"
                "${MEDIAMTX_DIST_DIR}/mediamtx.yml"
                "${MEDIAMTX_DIST_DIR}/LICENSE"
                "$<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources/"
            COMMAND chmod +x "$<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources/mediamtx"
            COMMENT "Bundling MediaMTX ${MEDIAMTX_VERSION} (${MEDIAMTX_PLATFORM}) into ${PROJECT_NAME}.mxo/Contents/Resources"
            VERBATIM
        )
    endif()
endif()

include(${MIN_API_DIR}/script/min-posttarget.cmake)
