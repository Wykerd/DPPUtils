cmake_minimum_required (VERSION 3.12)

project(webm-parser LANGUAGES CXX VERSION 1.0.0.28)

aux_source_directory("src" libsrc)
add_library(webm ${libsrc})

set_target_properties(webm PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(webm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} 
)

target_include_directories(webm PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# add_executable(webm_parser 
#     ${CMAKE_CURRENT_SOURCE_DIR}/demo/demo.cc
# )
# 
# target_link_libraries(webm_parser PUBLIC webm)
