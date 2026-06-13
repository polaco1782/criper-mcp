#include "criper/fs_tool_registry.hpp"

#include <fstream>

namespace criper {

namespace {

[[nodiscard]] json grep_schema() {
    json schema = base_schema();
    schema["properties"] = {
        {"pattern", {{"type", "string"}, {"description", "Text or regex to search for."}}},
        {"path", {{"type", "string"}, {"description", "File or directory to search. Default: ."}}},
        {"max_results", {{"type", "integer"}, {"minimum", 1}, {"description", "Maximum matches to return."}}},
        {"context", {{"type", "integer"}, {"minimum", 0}, {"description", "Include N lines before and after each match."}}},
        {"case_sensitive", {{"type", "boolean"}, {"description", "Case-sensitive search. Default: false"}}},
        {"regex", {{"type", "boolean"}, {"description", "Treat pattern as a regex. Default: false"}}},
    };
    schema["required"] = json::array({"pattern"});
    return schema;
}

} // namespace

json make_fs_grep_spec() {
    return make_tool_spec("fs_grep", "Search through files for matching text.", grep_schema());
}

json call_fs_grep(const FileToolsContext& context, const json& arguments) {
    const fs::path path = context.resolve_path(optional_string(arguments, "path", "."), true);
    const std::string pattern = require_string(arguments, "pattern");
    const std::size_t max_results = optional_size(arguments, "max_results", 100U);
    const std::size_t context_lines = optional_size(arguments, "context", 0U);
    const bool case_sensitive = optional_bool(arguments, "case_sensitive", false);
    const bool regex_mode = optional_bool(arguments, "regex", false);

    std::optional<std::regex> matcher;
    if (regex_mode) {
        const auto flags = case_sensitive
            ? std::regex::ECMAScript
            : static_cast<std::regex::flag_type>(std::regex::ECMAScript | std::regex::icase);
        matcher.emplace(pattern, flags);
    }

    auto line_matches = [&](const std::string& line) {
        return text_matches_pattern(line, pattern, case_sensitive, matcher ? &*matcher : nullptr);
    };

    auto search_file = [&](const fs::path& file_path, json& matches, std::size_t& count) {
        std::ifstream input(file_path);
        if (!input) {
            return;
        }

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(input, line)) {
            lines.push_back(line);
        }

        for (std::size_t line_index = 0; line_index < lines.size() && count < max_results; ++line_index) {
            if (!line_matches(lines[line_index])) {
                continue;
            }

            const std::size_t context_start = line_index > context_lines ? line_index - context_lines : 0U;
            const std::size_t context_end = std::min(lines.size(), line_index + context_lines + 1U);
            json surrounding = json::array();
            for (std::size_t context_index = context_start; context_index < context_end; ++context_index) {
                surrounding.push_back({
                    {"line", static_cast<std::uint64_t>(context_index + 1U)},
                    {"text", lines[context_index]},
                    {"match", context_index == line_index},
                });
            }

            matches.push_back({
                {"path", relative_string(context.root_path(), file_path)},
                {"line", static_cast<std::uint64_t>(line_index + 1U)},
                {"text", lines[line_index]},
                {"context", std::move(surrounding)},
            });
            ++count;
        }
    };

    json matches = json::array();
    std::size_t count = 0U;
    if (fs::is_regular_file(path)) {
        search_file(path, matches, count);
    } else if (fs::is_directory(path)) {
        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (count >= max_results) {
                break;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
            search_file(entry.path(), matches, count);
        }
    } else {
        throw ToolError("path is not a regular file or directory");
    }

    return json{
        {"path", relative_string(context.root_path(), path)},
        {"pattern", pattern},
        {"regex", regex_mode},
        {"case_sensitive", case_sensitive},
        {"matches", std::move(matches)},
        {"truncated", count >= max_results},
    };
}

} // namespace criper
