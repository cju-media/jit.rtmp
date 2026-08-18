# NOTE: the Max SDK's pretarget script derives the Max object name from THIS
# DIRECTORY'S NAME (not from any project() call here) - it replaces a trailing
# "_tilde" with "~", so the folder must be named "jit.rtmp.send_tilde" for the
# built external to come out as "jit.rtmp.send~.mxo".

set(MIN_API_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../min-api)
# The package directory is defined by the top-level CMakeLists; fall back to a
# sensible local path if this project is ever configured on its own.
if(NOT DEFINED C74_EXTERNALS_DIR)
    set(C74_EXTERNALS_DIR "${CMAKE_CURRENT_BINARY_DIR}/../jit.rtmp/externals")
endif()
set(C74_LIBRARY_OUTPUT_DIRECTORY "${C74_EXTERNALS_DIR}")
include(${MIN_API_DIR}/script/min-pretarget.cmake)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswscale
    libswresample
)

set(SOURCE_FILES
    source/jit.rtmp.send_tilde.cpp
)

add_library(${PROJECT_NAME} MODULE ${SOURCE_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${MIN_API_DIR}/include
    ${C74_INCLUDES}
)

target_link_libraries(${PROJECT_NAME} PUBLIC PkgConfig::FFMPEG)

if(APPLE)
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,-rpath,/opt/homebrew/lib" "-Wl,-rpath,/usr/local/lib")
endif()

include(${MIN_API_DIR}/script/min-posttarget.cmake)

# Warn (don't fail) if this rebuild is about to silently replace a universal
# binary - assembled by scripts/build-universal.sh - with a single-arch one.
if(APPLE)
    add_custom_command(TARGET ${PROJECT_NAME} PRE_LINK
        COMMAND "${CMAKE_SOURCE_DIR}/scripts/warn-if-clobbering-universal.sh"
            "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>" "${CMAKE_OSX_ARCHITECTURES}"
        VERBATIM
    )
endif()
