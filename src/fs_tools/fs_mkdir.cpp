#include "criper/fs_tool_registry.hpp"

namespace criper {

namespace {

[[nodiscard]] json mkdir_schema() {
    json schema = base_schema();
    schema["properties"] = {
        {"path", {{"type", "string"}}},
        {"parents", {{"type", "boolean"}}},
    };
    schema["required"] = json::array({"path"});
    return schema;
}

} // namespace

json make_fs_mkdir_spec() {
    return make_tool_spec("fs_mkdir", "Create a directory below the configured root.", mkdir_schema());
}

json call_fs_mkdir(const FileToolsContext& context, const json& arguments) {
    const fs::path path = context.resolve_path(require_string(arguments, "path"), false);
    const bool parents = optional_bool(arguments, "parents", true);
    const bool created = parents ? fs::create_directories(path) : fs::create_directory(path);

    return json{
        {"path", relative_string(context.root_path(), path)},
        {"created", created},
    };
}

} // namespace criper
