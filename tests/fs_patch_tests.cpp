#include "criper/fs_tool_registry.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

namespace fs = std::filesystem;
using criper::FileToolsContext;
using criper::json;

class TempDir {
public:
    TempDir() {
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        path_ = fs::temp_directory_path() / ("criper-mcp-fs-patch-tests-" + suffix);
        fs::create_directories(path_);
    }

    ~TempDir() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    [[nodiscard]] const fs::path& path() const noexcept {
        return path_;
    }

private:
    fs::path path_;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_file(const fs::path& path, const std::string& content) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(stream), "failed to open test file for writing: " + path.string());
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    stream.flush();
    require(static_cast<bool>(stream), "failed to write test file: " + path.string());
}

std::string read_file(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    require(static_cast<bool>(stream), "failed to open test file for reading: " + path.string());

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

void expect_single_match(const json& result, const std::string& expected_type) {
    require(result.value("path", "") == "sample.txt", "patch result path should be relative to the root");
    require(result.contains("operations"), "patch result should include operations");
    require(result["operations"].is_array(), "patch result operations should be an array");
    require(result["operations"].size() == 1U, "patch result should include one operation");

    const json& operation = result["operations"].front();
    require(operation.value("type", "") == expected_type, "patch result should include operation type: " + expected_type);
    require(operation.value("matches", 0U) == 1U, "patch result should include one match");
}

void replace_persists_to_disk(const FileToolsContext& context, const fs::path& file_path) {
    write_file(file_path, "alpha beta gamma\n");

    const json result = criper::call_fs_patch(context, {
        {"path", "sample.txt"},
        {"op", "replace"},
        {"find", "beta"},
        {"replace", "delta"},
    });

    expect_single_match(result, "replace");
    require(read_file(file_path) == "alpha delta gamma\n", "replace patch should persist to disk");
}

void delete_persists_to_disk(const FileToolsContext& context, const fs::path& file_path) {
    write_file(file_path, "alpha beta gamma\n");

    const json result = criper::call_fs_patch(context, {
        {"path", "sample.txt"},
        {"op", "delete"},
        {"find", "beta "},
    });

    expect_single_match(result, "delete");
    require(read_file(file_path) == "alpha gamma\n", "delete patch should persist to disk");
}

void insert_before_persists_to_disk(const FileToolsContext& context, const fs::path& file_path) {
    write_file(file_path, "alpha gamma\n");

    const json result = criper::call_fs_patch(context, {
        {"path", "sample.txt"},
        {"op", "insert_before"},
        {"find", "gamma"},
        {"content", "beta "},
    });

    expect_single_match(result, "insert_before");
    require(read_file(file_path) == "alpha beta gamma\n", "insert_before patch should persist to disk");
}

void insert_after_persists_to_disk(const FileToolsContext& context, const fs::path& file_path) {
    write_file(file_path, "alpha gamma\n");

    const json result = criper::call_fs_patch(context, {
        {"path", "sample.txt"},
        {"op", "insert_after"},
        {"find", "alpha "},
        {"content", "beta "},
    });

    expect_single_match(result, "insert_after");
    require(read_file(file_path) == "alpha beta gamma\n", "insert_after patch should persist to disk");
}

} // namespace

int main() {
    try {
        TempDir temp_dir;
        const fs::path file_path = temp_dir.path() / "sample.txt";
        const FileToolsContext context(temp_dir.path(), false, false);

        replace_persists_to_disk(context, file_path);
        delete_persists_to_disk(context, file_path);
        insert_before_persists_to_disk(context, file_path);
        insert_after_persists_to_disk(context, file_path);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
