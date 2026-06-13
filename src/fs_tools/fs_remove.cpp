#include "criper/fs_tool_registry.hpp"

namespace criper {

namespace {

[[nodiscard]] json remove_schema() {
    json schema = base_schema();
    schema["properties"] = {
        {"path", {{"type", "string"}}},
        {"recursive", {{"type", "boolean"}}},
    };
    schema["required"] = json::array({"path"});
    return schema;
}

} // namespace

json make_fs_remove_spec() {
    return make_tool_spec("fs_remove", "Remove a file, symlink, or directory inside the configured root.", remove_schema());
}

json call_fs_remove(const FileToolsContext& context, const json& arguments) {
    const fs::path path = context.resolve_path(require_string(arguments, "path"), true);
    const bool recursive = optional_bool(arguments, "recursive", false);

    const std::uint64_t removed = (recursive && fs::is_directory(path) && !fs::is_symlink(path))
        ? fs::remove_all(path)
        : (fs::remove(path) ? 1U : 0U);

    return json{
        {"path", relative_string(context.root_path(), path)},
        {"removed", removed},
    };
}

} // namespace criper
