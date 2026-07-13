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
        path_ = fs::temp_directory_path() / ("criper-mcp-git-tool-tests-" + suffix);
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

void init_add_commit_and_log(const FileToolsContext& context, const fs::path& root) {
    const json init = criper::call_git(context, {{"op", "init"}, {"path", "repo"}});
    require(init.value("path", "") == "repo", "init should return relative repo path");

    fs::create_directories(root / "repo");
    write_file(root / "repo" / "sample.txt", "alpha\n");

    const json status = criper::call_git(context, {{"op", "status"}, {"path", "repo"}});
    require(!status.value("clean", true), "new file should make status dirty");

    criper::call_git(context, {{"op", "add"}, {"path", "repo"}, {"paths", json::array({"sample.txt"})}});
    const json commit = criper::call_git(context, {
        {"op", "commit"},
        {"path", "repo"},
        {"message", "initial"},
        {"author_name", "Test Author"},
        {"author_email", "test@example.invalid"},
    });
    require(!commit.value("oid", "").empty(), "commit should return oid");

    const json log = criper::call_git(context, {{"op", "log"}, {"path", "repo"}, {"max_count", 1}});
    require(log["commits"].is_array() && log["commits"].size() == 1U, "log should include one commit");
    require(log["commits"].front().value("summary", "") == "initial", "log should include commit summary");
}

void branch_checkout_reset_guards(const FileToolsContext& context, const fs::path& root) {
    criper::call_git(context, {{"op", "branch_create"}, {"path", "repo"}, {"name", "feature"}});
    const json branches = criper::call_git(context, {{"op", "branches"}, {"path", "repo"}});
    require(branches["branches"].is_array() && branches["branches"].size() >= 2U, "branches should include feature branch");

    bool delete_blocked = false;
    try {
        criper::call_git(context, {{"op", "branch_delete"}, {"path", "repo"}, {"name", "feature"}});
    } catch (const criper::ToolError&) {
        delete_blocked = true;
    }
    require(delete_blocked, "branch delete should require force=true");

    criper::call_git(context, {{"op", "branch_delete"}, {"path", "repo"}, {"name", "feature"}, {"force", true}});

    write_file(root / "repo" / "sample.txt", "changed\n");
    bool hard_reset_blocked = false;
    try {
        criper::call_git(context, {{"op", "reset"}, {"path", "repo"}, {"mode", "hard"}});
    } catch (const criper::ToolError&) {
        hard_reset_blocked = true;
    }
    require(hard_reset_blocked, "hard reset should require force=true");
}

void path_containment_is_enforced(const FileToolsContext& context) {
    bool blocked = false;
    try {
        criper::call_git(context, {{"op", "init"}, {"path", "../outside"}});
    } catch (const criper::ToolError&) {
        blocked = true;
    }
    require(blocked, "git paths outside root should be rejected");
}

void redacts_credentials() {
    const json redacted = criper::redact_git_arguments({
        {"op", "fetch"},
        {"credentials", {
            {"username", "alice"},
            {"password", "pw"},
            {"token", "tok"},
            {"ssh_private_key_path", "key"},
            {"ssh_passphrase", "phrase"},
        }},
    });

    require(redacted["credentials"].get<std::string>() == "[redacted]", "credentials object should be redacted");
}

} // namespace

int main() {
    try {
        TempDir temp_dir;
        const FileToolsContext context(temp_dir.path(), false, false);
        init_add_commit_and_log(context, temp_dir.path());
        branch_checkout_reset_guards(context, temp_dir.path());
        path_containment_is_enforced(context);
        redacts_credentials();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
