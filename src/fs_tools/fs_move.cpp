#include "criper/fs_tool_registry.hpp"

#include <system_error>

namespace criper {

namespace {

[[nodiscard]] json move_schema() {
    json schema = base_schema();
    schema["properties"] = {
        {"from", {{"type", "string"}, {"description", "Source file or directory."}}},
        {"to", {{"type", "string"}, {"description", "Destination path."}}},
        {"overwrite", {{"type", "boolean"}, {"description", "Remove an existing destination first. Default: false"}}},
        {"create_parents", {{"type", "boolean"}, {"description", "Create destination parent directories if needed. Default: true"}}},
    };
    schema["required"] = json::array({"from", "to"});
    return schema;
}

} // namespace

json make_fs_move_spec() {
    return make_tool_spec("fs_move", "Move or rename a file or directory inside the configured root.", move_schema());
}

json call_fs_move(const FileToolsContext& context, const json& arguments) {
    const fs::path source = context.resolve_path(require_string(arguments, "from"), true);
    const fs::path destination = context.resolve_path(require_string(arguments, "to"), false);
    const bool overwrite = optional_bool(arguments, "overwrite", false);
    const bool create_parents = optional_bool(arguments, "create_parents", true);

    if (source == destination) {
        throw ToolError("source and destination are the same");
    }
    if (!overwrite && fs::exists(destination)) {
        throw ToolError("destination already exists");
    }
    if (create_parents) {
        fs::create_directories(destination.parent_path());
    }
    if (overwrite) {
        remove_existing_path(destination);
    }

    std::error_code error;
    fs::rename(source, destination, error);
    if (error == std::errc::cross_device_link) {
        fs::copy_options options = fs::copy_options::copy_symlinks | fs::copy_options::recursive;
        if (overwrite) {
            options |= fs::copy_options::overwrite_existing;
        }
        fs::copy(source, destination, options);
        remove_existing_path(source);
    } else if (error) {
        throw fs::filesystem_error("failed to move path", source, destination, error);
    }

    return json{
        {"from", relative_string(context.root_path(), source)},
        {"to", relative_string(context.root_path(), destination)},
        {"overwrite", overwrite},
    };
}

} // namespace criper
