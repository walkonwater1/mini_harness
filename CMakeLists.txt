cmake_minimum_required(VERSION 3.10)

project(mini_harness)

set(CMAKE_CXX_STANDARD 17)

find_package(CURL REQUIRED)

add_executable(mini_harness main.cpp)

include(CheckCXXSourceCompiles)
check_cxx_source_compiles(
    "#include <filesystem>\nint main(){return 0;}"
    FS_HEADER_ONLY)

target_link_libraries(mini_harness CURL::libcurl)

if(NOT FS_HEADER_ONLY)
    target_link_libraries(mini_harness stdc++fs)
endif()
