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

option(DPP_BUILD_TEST "" OFF)
include(FetchContent)

FetchContent_Declare(
        libdpp
        GIT_REPOSITORY https://github.com/brainboxdotcc/DPP.git
        GIT_TAG d8e8ad6be1905c5859cedfb0972ce5f6ed5fbd33)

FetchContent_GetProperties(libdpp)
if(NOT libdpp_POPULATED)
    FetchContent_Populate(libdpp)
    target_include_directories(test PUBLIC
        ${libdpp_SOURCE_DIR}/include
    )
    add_subdirectory(
        ${libdpp_SOURCE_DIR}
        ${libdpp_BINARY_DIR}
        EXCLUDE_FROM_ALL)
    target_link_libraries(test PUBLIC dpp)
endif()

target_link_libraries(test PUBLIC dpputils-ytplayer)
