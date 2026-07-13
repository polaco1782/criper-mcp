cmake_minimum_required(VERSION 3.18)

project(criper_mcp_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Threads REQUIRED)

set(CRIPER_DEPS_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps")
set(CRIPER_LIBSSH2_ARCHIVE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libssh2-1.11.1.tar.gz")
set(CRIPER_LIBSSH2_SOURCE_DIR "${CRIPER_DEPS_DIR}/libssh2-1.11.1")
set(CRIPER_LIBGIT2_ARCHIVE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libgit2-1.9.1.tar.gz")
set(CRIPER_LIBGIT2_SOURCE_DIR "${CRIPER_DEPS_DIR}/libgit2-1.9.1")

function(criper_extract_dependency archive source_dir marker_file)
    if(NOT EXISTS "${archive}")
        message(FATAL_ERROR "Missing vendored dependency archive: ${archive}")
    endif()

    file(SHA256 "${archive}" archive_hash)
    set(stamp_file "${source_dir}/.criper_archive_sha256")
    set(existing_hash "")
    if(EXISTS "${stamp_file}")
        file(READ "${stamp_file}" existing_hash)
    endif()

    if(NOT EXISTS "${source_dir}/${marker_file}" OR NOT existing_hash STREQUAL archive_hash)
        file(REMOVE_RECURSE "${source_dir}")
        file(MAKE_DIRECTORY "${source_dir}")
        file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${source_dir}")
        file(WRITE "${stamp_file}" "${archive_hash}")
    endif()
endfunction()

criper_extract_dependency("${CRIPER_LIBSSH2_ARCHIVE}" "${CRIPER_LIBSSH2_SOURCE_DIR}" "CMakeLists.txt")
criper_extract_dependency("${CRIPER_LIBGIT2_ARCHIVE}" "${CRIPER_LIBGIT2_SOURCE_DIR}" "CMakeLists.txt")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build vendored dependencies as static libraries" FORCE)
set(CMAKE_EXPORT_PACKAGE_REGISTRY OFF CACHE BOOL "Do not export vendored packages to the user package registry" FORCE)
set(OPENSSL_USE_STATIC_LIBS ON CACHE BOOL "Prefer static OpenSSL archives for vendored git dependencies" FORCE)
set(ZLIB_USE_STATIC_LIBS ON CACHE BOOL "Prefer static zlib archives for vendored git dependencies" FORCE)
find_library(CRIPER_STATIC_ZLIB NAMES libz.a)
if(CRIPER_STATIC_ZLIB)
    set(ZLIB_LIBRARY_RELEASE "${CRIPER_STATIC_ZLIB}" CACHE FILEPATH "Static zlib archive" FORCE)
    set(ZLIB_LIBRARY "${CRIPER_STATIC_ZLIB}" CACHE FILEPATH "Static zlib archive" FORCE)
endif()
set(BUILD_STATIC_LIBS ON CACHE BOOL "Build vendored libssh2 static library" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Do not build vendored dependency tests" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Do not build vendored dependency examples" FORCE)
set(CRYPTO_BACKEND OpenSSL CACHE STRING "Use OpenSSL for vendored libssh2" FORCE)
add_subdirectory("${CRIPER_LIBSSH2_SOURCE_DIR}" "${CRIPER_DEPS_DIR}/libssh2-build" EXCLUDE_FROM_ALL)
target_compile_definitions(libssh2_static PRIVATE _DEFAULT_SOURCE)

set(BUILD_TESTS OFF CACHE BOOL "Do not build vendored libgit2 tests" FORCE)
set(BUILD_CLI OFF CACHE BOOL "Do not build vendored libgit2 CLI" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Do not build vendored libgit2 examples" FORCE)
set(USE_HTTPS OpenSSL CACHE STRING "Use OpenSSL for libgit2 HTTPS support" FORCE)
set(USE_SSH libssh2 CACHE STRING "Use libssh2 for libgit2 SSH support" FORCE)
set(USE_BUNDLED_ZLIB ON CACHE BOOL "Use libgit2 bundled zlib" FORCE)
set(USE_NSEC OFF CACHE BOOL "Disable libgit2 nanosecond stat fields for portable vendored builds" FORCE)
add_subdirectory("${CRIPER_LIBGIT2_SOURCE_DIR}" "${CRIPER_DEPS_DIR}/libgit2-build" EXCLUDE_FROM_ALL)

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
    src/fs_tools/git.cpp
    src/sandbox_setup.cpp
    src/sandbox.cpp
)

target_include_directories(criper-mcp
    PRIVATE
        include
        third_party
        "${CRIPER_LIBGIT2_SOURCE_DIR}/include"
)

target_link_libraries(criper-mcp
    PRIVATE
        libgit2package
        Threads::Threads
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

add_executable(git-tool-tests
    tests/git_tool_tests.cpp
    src/fs_tools/support.cpp
    src/fs_tools/git.cpp
)

target_include_directories(git-tool-tests
    PRIVATE
        include
        third_party
        "${CRIPER_LIBGIT2_SOURCE_DIR}/include"
)

target_link_libraries(git-tool-tests
    PRIVATE
        libgit2package
        Threads::Threads
)

target_compile_options(git-tool-tests
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME git-tool-tests COMMAND git-tool-tests)
