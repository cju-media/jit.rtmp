cmake_minimum_required(VERSION 3.19)

# Homebrew's FFmpeg on Apple Silicon ships arm64-only dylibs. The Max SDK's
# pretarget script defaults CMAKE_OSX_ARCHITECTURES to x86_64 when unset, which
# would fail to link against them, so pin to the native arch before any
# project() call happens (including the one inside min-pretarget.cmake).
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    execute_process(COMMAND uname -m OUTPUT_VARIABLE HOST_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(CMAKE_OSX_ARCHITECTURES "${HOST_ARCH}" CACHE STRING "Build architecture" FORCE)
endif()

project(rtmp-max-output NONE)

add_subdirectory(jit.rtmp.send_tilde)
