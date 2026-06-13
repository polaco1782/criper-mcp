#include "criper/fs_tool_registry.hpp"

namespace criper {

namespace {

[[nodiscard]] json copy_schema() {
    json schema = base_schema();
    schema["properties"] = {
        {"from", {{"type", "string"}, {"description", "Source file or directory."}}},
        {"to", {{"type", "string"}, {"description", "Destination path."}}},
        {"recursive", {{"type", "boolean"}, {"description", "Required for directory copies. Default: false"}}},
        {"overwrite", {{"type", "boolean"}, {"description", "Replace existing files at the destination when possible. Default: false"}}},
        {"create_parents", {{"type", "boolean"}, {"description", "Create destination parent directories if needed. Default: true"}}},
    };
    schema["required"] = json::array({"from", "to"});
    return schema;
}

} // namespace

json make_fs_copy_spec() {
    return make_tool_spec("fs_copy", "Copy a file or directory inside the configured root.", copy_schema());
}

json call_fs_copy(const FileToolsContext& context, const json& arguments) {
    const fs::path source = context.resolve_path(require_string(arguments, "from"), true);
    const fs::path destination = context.resolve_path(require_string(arguments, "to"), false);
    const bool recursive = optional_bool(arguments, "recursive", false);
    const bool overwrite = optional_bool(arguments, "overwrite", false);
    const bool create_parents = optional_bool(arguments, "create_parents", true);

    if (source == destination) {
        throw ToolError("source and destination are the same");
    }
    if (fs::is_directory(source) && !recursive) {
        throw ToolError("directory copies require recursive=true");
    }
    if (!overwrite && fs::exists(destination)) {
        throw ToolError("destination already exists");
    }
    if (create_parents) {
        fs::create_directories(destination.parent_path());
    }

    fs::copy_options options = fs::copy_options::copy_symlinks;
    if (recursive) {
        options |= fs::copy_options::recursive;
    }
    if (overwrite) {
        options |= fs::copy_options::overwrite_existing;
    }

    fs::copy(source, destination, options);
    return json{
        {"from", relative_string(context.root_path(), source)},
        {"to", relative_string(context.root_path(), destination)},
        {"recursive", recursive},
        {"overwrite", overwrite},
    };
}

} // namespace criper
