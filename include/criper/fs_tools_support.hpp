#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "json.hpp"

namespace criper {

namespace fs = std::filesystem;
using json = nlohmann::json;

class ToolError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ExecResult {
    int exit_code = -1;
    bool timed_out = false;
    std::string stdout_text;
    std::string stderr_text;
};

class FileToolsContext {
public:
    FileToolsContext(fs::path root_path, bool debug_enabled);

    [[nodiscard]] const fs::path& root_path() const noexcept;
    [[nodiscard]] bool debug_enabled() const noexcept;
    [[nodiscard]] fs::path resolve_path(const std::string& raw_path, bool must_exist) const;
    [[nodiscard]] json make_operation_from_arguments(const json& arguments) const;

private:
    [[nodiscard]] fs::path normalize_client_path(const fs::path& input_path) const;

    fs::path root_path_;
    bool debug_enabled_;
};

[[nodiscard]] std::string truncate_for_log(std::string_view text, std::size_t limit = 4096U);
void debug_log(bool enabled, const std::string& message);

[[nodiscard]] bool path_has_prefix(const fs::path& prefix, const fs::path& path);
[[nodiscard]] std::string permissions_to_octal(fs::perms permissions);
[[nodiscard]] fs::perms octal_to_permissions(const std::string& octal);

[[nodiscard]] const json& require_object(const json& value, std::string_view label);
[[nodiscard]] const json& require_array(const json& value, std::string_view label);
[[nodiscard]] std::string require_string(const json& object, std::string_view key);
[[nodiscard]] std::string optional_string(const json& object, std::string_view key, std::string fallback = {});
[[nodiscard]] bool optional_bool(const json& object, std::string_view key, bool fallback = false);
[[nodiscard]] std::size_t optional_size(const json& object, std::string_view key, std::size_t fallback);
[[nodiscard]] std::uint64_t optional_u64(const json& object, std::string_view key, std::uint64_t fallback);

[[nodiscard]] std::string relative_string(const fs::path& root_path, const fs::path& path);
[[nodiscard]] json make_tool_spec(std::string_view name, std::string_view description, json input_schema);
[[nodiscard]] json base_schema();
[[nodiscard]] json make_tool_result(json structured);
[[nodiscard]] json make_tool_error(std::string message);
[[nodiscard]] json make_stat_object(const fs::path& root_path, const fs::path& path);
[[nodiscard]] bool is_default_ignored_directory(const fs::directory_entry& entry);

[[nodiscard]] std::string lowercase_copy(std::string_view text);
[[nodiscard]] bool text_matches_pattern(
    std::string_view text,
    std::string_view pattern,
    bool case_sensitive,
    const std::regex* regex_matcher
);

void remove_existing_path(const fs::path& path);

[[nodiscard]] ExecResult execute_command(
    const fs::path& working_directory,
    const std::vector<std::string>& arguments,
    const std::map<std::string, std::string>& extra_environment,
    const std::string& stdin_text,
    std::chrono::seconds timeout
);

[[nodiscard]] std::size_t replace_single(std::string& content, const std::string& needle, const std::string& replacement);
[[nodiscard]] std::size_t replace_all(std::string& content, const std::string& needle, const std::string& replacement);
[[nodiscard]] std::size_t insert_relative(
    std::string& content,
    const std::string& needle,
    const std::string& payload,
    bool after
);
[[nodiscard]] std::size_t delete_single(std::string& content, const std::string& needle);

} // namespace criper
