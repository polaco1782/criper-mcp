#include "criper/fs_tool_registry.hpp"

#include <deque>
#include <fstream>
#include <sstream>
#include <utility>

namespace criper {

namespace {

[[nodiscard]] json tail_schema() {
    json schema = base_schema();
    schema["properties"] = {
        {"path", {{"type", "string"}, {"description", "File to read from the end."}}},
        {"lines", {{"type", "integer"}, {"minimum", 1}, {"description", "Number of trailing lines to return. Default: 40"}}},
    };
    schema["required"] = json::array({"path"});
    return schema;
}

} // namespace

json make_fs_tail_spec() {
    return make_tool_spec("fs_tail", "Read the last lines of a text file from the configured root.", tail_schema());
}

json call_fs_tail(const FileToolsContext& context, const json& arguments) {
    const fs::path path = context.resolve_path(require_string(arguments, "path"), true);
    if (!fs::is_regular_file(path)) {
        throw ToolError("path is not a regular file");
    }

    const std::size_t requested_lines = optional_size(arguments, "lines", 40U);
    if (requested_lines == 0U) {
        throw ToolError("lines must be greater than zero");
    }

    std::ifstream input(path);
    std::deque<std::pair<std::uint64_t, std::string>> trailing_lines;
    std::string line;
    std::uint64_t total_lines = 0U;
    while (std::getline(input, line)) {
        ++total_lines;
        trailing_lines.emplace_back(total_lines, line);
        if (trailing_lines.size() > requested_lines) {
            trailing_lines.pop_front();
        }
    }

    json lines = json::array();
    std::ostringstream content;
    bool first = true;
    for (const auto& [line_number, text] : trailing_lines) {
        lines.push_back({
            {"line", line_number},
            {"text", text},
        });
        if (!first) {
            content << '\n';
        }
        first = false;
        content << text;
    }

    return json{
        {"path", relative_string(context.root_path(), path)},
        {"lines_requested", static_cast<std::uint64_t>(requested_lines)},
        {"line_count", total_lines},
        {"start_line", trailing_lines.empty() ? 0U : trailing_lines.front().first},
        {"truncated", total_lines > trailing_lines.size()},
        {"lines", std::move(lines)},
        {"content", content.str()},
    };
}

} // namespace criper
