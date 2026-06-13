#include "criper/fs_tool_registry.hpp"

namespace criper {

namespace {

[[nodiscard]] json find_schema() {
    json schema = base_schema();
    schema["properties"] = {
        {"pattern", {{"type", "string"}, {"description", "Filename or relative path pattern to search for."}}},
        {"path", {{"type", "string"}, {"description", "Directory or file to search from. Default: ."}}},
        {"recursive", {{"type", "boolean"}, {"description", "Search nested directories too. Default: true"}}},
        {"max_results", {{"type", "integer"}, {"minimum", 1}, {"description", "Maximum matches to return."}}},
        {"case_sensitive", {{"type", "boolean"}, {"description", "Case-sensitive search. Default: false"}}},
        {"regex", {{"type", "boolean"}, {"description", "Treat pattern as a regex. Default: false"}}},
        {"kind", {
            {"type", "string"},
            {"enum", json::array({"any", "file", "directory", "symlink"})},
            {"description", "Optional entry type filter. Default: any"},
        }},
    };
    schema["required"] = json::array({"pattern"});
    return schema;
}

} // namespace

json make_fs_find_spec() {
    return make_tool_spec("fs_find", "Search for files and directories by name or relative path.", find_schema());
}

json call_fs_find(const FileToolsContext& context, const json& arguments) {
    const fs::path path = context.resolve_path(optional_string(arguments, "path", "."), true);
    const std::string pattern = require_string(arguments, "pattern");
    const bool recursive = optional_bool(arguments, "recursive", true);
    const std::size_t max_results = optional_size(arguments, "max_results", 100U);
    const bool case_sensitive = optional_bool(arguments, "case_sensitive", false);
    const bool regex_mode = optional_bool(arguments, "regex", false);
    const std::string kind = optional_string(arguments, "kind", "any");

    if (kind != "any" && kind != "file" && kind != "directory" && kind != "symlink") {
        throw ToolError("unsupported kind filter: " + kind);
    }

    std::optional<std::regex> matcher;
    if (regex_mode) {
        const auto flags = case_sensitive
            ? std::regex::ECMAScript
            : static_cast<std::regex::flag_type>(std::regex::ECMAScript | std::regex::icase);
        matcher.emplace(pattern, flags);
    }

    auto kind_matches = [&](const fs::file_status status) {
        if (kind == "any") {
            return true;
        }
        if (kind == "file") {
            return fs::is_regular_file(status);
        }
        if (kind == "directory") {
            return fs::is_directory(status);
        }
        return fs::is_symlink(status);
    };

    json matches = json::array();
    std::size_t count = 0U;
    bool truncated = false;

    const auto try_push_match = [&](const fs::path& candidate_path, const std::string_view candidate_text) {
        const fs::file_status status = fs::symlink_status(candidate_path);
        if (!kind_matches(status)) {
            return true;
        }
        if (!text_matches_pattern(candidate_text, pattern, case_sensitive, matcher ? &*matcher : nullptr)) {
            return true;
        }
        if (count >= max_results) {
            truncated = true;
            return false;
        }

        matches.push_back(make_stat_object(context.root_path(), candidate_path));
        ++count;
        return true;
    };

    if (fs::is_directory(path)) {
        if (recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(path)) {
                const std::string relative_candidate = fs::relative(entry.path(), path).generic_string();
                if (!try_push_match(entry.path(), relative_candidate)) {
                    break;
                }
            }
        } else {
            for (const auto& entry : fs::directory_iterator(path)) {
                const std::string relative_candidate = entry.path().filename().generic_string();
                if (!try_push_match(entry.path(), relative_candidate)) {
                    break;
                }
            }
        }
    } else {
        const std::string candidate = path.filename().generic_string();
        (void)try_push_match(path, candidate);
    }

    return json{
        {"path", relative_string(context.root_path(), path)},
        {"pattern", pattern},
        {"recursive", recursive},
        {"regex", regex_mode},
        {"case_sensitive", case_sensitive},
        {"kind", kind},
        {"matches", std::move(matches)},
        {"truncated", truncated},
    };
}

} // namespace criper
