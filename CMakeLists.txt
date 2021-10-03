cmake_minimum_required(VERSION 3.21)

project(dpputils LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)  

option(YTDL_BUILD_MUX "" OFF)
option(YTDL_BUILD_CLI "" OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/deps/ytdl)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/deps/webm_parser)

add_library(dpputils-ytplayer
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/ytplayer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/partial_buffer_reader.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/stream_callback.cc
)

target_include_directories(dpputils-ytplayer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dpputils-ytplayer PUBLIC
    ytdlhttp 
    ytdlcore
    webm
)

add_executable(test
    ${CMAKE_CURRENT_SOURCE_DIR}/src/test.cc
)

target_link_libraries(test PUBLIC dpputils-ytplayer)
