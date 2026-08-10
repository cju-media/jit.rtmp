cmake_minimum_required(VERSION 3.19)

# Homebrew's FFmpeg on Apple Silicon ships arm64-only dylibs. The Max SDK's
# pretarget script defaults CMAKE_OSX_ARCHITECTURES to x86_64 when unset, which
# would fail to link against them, so pin to the native arch before any
# project() call happens (including the one inside min-pretarget.cmake).
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    execute_process(COMMAND uname -m OUTPUT_VARIABLE HOST_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(CMAKE_OSX_ARCHITECTURES "${HOST_ARCH}" CACHE STRING "Build architecture" FORCE)
endif()

project(jit-rtmp NONE)

# ---------------------------------------------------------------------------
# Max package layout
#
# Everything a Max package needs is assembled under build/jit.rtmp/, so that one
# directory *is* the package and can be symlinked straight into
# ~/Documents/Max 9/Packages/. Previously the externals landed in a build
# subfolder and the package was wired into Max by hand, which meant a stale
# .mxo was always one forgotten copy away.
#
# Link it into Max with:
#   ln -s "$(pwd)/build/jit.rtmp" ~/Documents/"Max 9"/Packages/jit.rtmp
# ---------------------------------------------------------------------------
set(C74_PACKAGE_NAME "jit.rtmp")
set(C74_PACKAGE_DIR  "${CMAKE_BINARY_DIR}/${C74_PACKAGE_NAME}")
set(C74_EXTERNALS_DIR "${C74_PACKAGE_DIR}/externals")

file(MAKE_DIRECTORY "${C74_EXTERNALS_DIR}")

# Symlink rather than copy the authored folders: a copy goes stale the moment
# a patcher is edited, and these are the files most likely to be edited while
# Max is open.
foreach(_authored patchers docs extras help media init)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_authored}")
        file(CREATE_LINK
            "${CMAKE_CURRENT_SOURCE_DIR}/${_authored}"
            "${C74_PACKAGE_DIR}/${_authored}"
            SYMBOLIC)
    endif()
endforeach()

file(WRITE "${C74_PACKAGE_DIR}/package-info.json"
"{
\t\"name\": \"jit.rtmp\",
\t\"version\": \"0.1.0\",
\t\"author\": \"Cameron Johnston\",
\t\"description\": \"Send and receive RTMP video/audio streams in Jitter and MSP.\",
\t\"tags\": ["jitter", "video", "audio", "rtmp", "network"]
}
")

add_subdirectory(jit.rtmp.send_tilde)
add_subdirectory(jit.rtmp.receive_tilde)
add_subdirectory(jit.rtmp.server)
