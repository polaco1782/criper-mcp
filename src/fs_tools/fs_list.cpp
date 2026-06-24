#include "criper/fs_tool_registry.hpp"

namespace criper {

namespace {

[[nodiscard]] json list_schema() {
    json schema = base_schema();
    schema["properties"] = {
        {"path", {{"type", "string"}, {"description", "Directory to list. Default: ."}}},
        {"recursive", {{"type", "boolean"}, {"description", "List nested files too."}}},
        {"max_entries", {{"type", "integer"}, {"minimum", 1}, {"description", "Maximum entries to return."}}},
    };
    return schema;
}

} // namespace

json make_fs_list_spec() {
    return make_tool_spec("fs_list", "List files and directories below the configured root.", list_schema());
}

json call_fs_list(const FileToolsContext& context, const json& arguments) {
    const fs::path path = context.resolve_path(optional_string(arguments, "path", "."), true);
    const bool recursive = optional_bool(arguments, "recursive", false);
    const std::size_t max_entries = optional_size(arguments, "max_entries", 512U);
    if (!fs::is_directory(path)) {
        throw ToolError("path is not a directory");
    }

    json entries = json::array();
    std::size_t count = 0U;
    bool truncated = false;

    const auto push_entry = [&](const fs::directory_entry& entry) {
        if (count >= max_entries) {
            truncated = true;
            return false;
        }
        entries.push_back(make_stat_object(context.root_path(), entry.path()));
        ++count;
        return true;
    };

    if (recursive) {
        for (fs::recursive_directory_iterator it(path), end; it != end; ++it) {
            const auto& entry = *it;
            if (is_default_ignored_directory(entry)) {
                it.disable_recursion_pending();
                continue;
            }
            if (!push_entry(entry)) {
                break;
            }
        }
    } else {
        for (const auto& entry : fs::directory_iterator(path)) {
            if (!push_entry(entry)) {
                break;
            }
        }
    }

    return json{
        {"path", relative_string(context.root_path(), path)},
        {"recursive", recursive},
        {"entries", std::move(entries)},
        {"truncated", truncated},
    };
}

} // namespace criper
