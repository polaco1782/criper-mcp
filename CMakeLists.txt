cmake_minimum_required(VERSION 3.18)

project(criper_mcp_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Threads REQUIRED)

add_executable(criper-mcp
    src/main.cpp
    src/mcp_server.cpp
    src/fs_tools/support.cpp
    src/fs_tools/dispatcher.cpp
    src/fs_tools/fs_list.cpp
    src/fs_tools/fs_find.cpp
    src/fs_tools/fs_read.cpp
    src/fs_tools/fs_tail.cpp
    src/fs_tools/fs_write.cpp
    src/fs_tools/fs_patch.cpp
    src/fs_tools/fs_grep.cpp
    src/fs_tools/fs_exec.cpp
    src/fs_tools/fs_stat.cpp
    src/fs_tools/fs_chmod.cpp
    src/fs_tools/fs_mkdir.cpp
    src/fs_tools/fs_copy.cpp
    src/fs_tools/fs_move.cpp
    src/fs_tools/fs_remove.cpp
    src/sandbox_setup.cpp
    src/sandbox.cpp
)

target_include_directories(criper-mcp
    PRIVATE
        include
        third_party
)

target_link_libraries(criper-mcp
    PRIVATE

)

target_compile_options(criper-mcp
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

enable_testing()

add_executable(fs-patch-tests
    tests/fs_patch_tests.cpp
    src/fs_tools/support.cpp
    src/fs_tools/fs_patch.cpp
)

target_include_directories(fs-patch-tests
    PRIVATE
        include
        third_party
)

target_link_libraries(fs-patch-tests
    PRIVATE
        Threads::Threads
)

target_compile_options(fs-patch-tests
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME fs-patch-tests COMMAND fs-patch-tests)
