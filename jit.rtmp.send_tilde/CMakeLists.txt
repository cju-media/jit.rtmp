# NOTE: the Max SDK's pretarget script derives the Max object name from THIS
# DIRECTORY'S NAME (not from any project() call here) - it replaces a trailing
# "_tilde" with "~", so the folder must be named "jit.rtmp.send_tilde" for the
# built external to come out as "jit.rtmp.send~.mxo".

set(MIN_API_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../min-api)
set(C74_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/../externals")
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
