cmake_minimum_required(VERSION 3.10)

project(mini_harness)

set(CMAKE_CXX_STANDARD 17)

find_package(CURL REQUIRED)

# 源文件
add_executable(mini_harness
    src/main.cpp
    src/harness_engine.cpp
)

# 头文件搜索路径
target_include_directories(mini_harness PRIVATE include)

include(CheckCXXSourceCompiles)
check_cxx_source_compiles(
    "#include <filesystem>\nint main(){return 0;}"
    FS_HEADER_ONLY)

target_link_libraries(mini_harness CURL::libcurl)

if(NOT FS_HEADER_ONLY)
    target_link_libraries(mini_harness stdc++fs)
endif()

# 生成 compile_commands.json，便于 AI 理解项目结构
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
