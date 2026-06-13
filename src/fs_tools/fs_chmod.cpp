#include "criper/fs_tool_registry.hpp"

namespace criper {

namespace {

[[nodiscard]] json chmod_schema() {
    json schema = base_schema();
    schema["properties"] = {
        {"path", {{"type", "string"}}},
        {"permissions", {{"type", "string"}}},
    };
    schema["required"] = json::array({"path", "permissions"});
    return schema;
}

} // namespace

json make_fs_chmod_spec() {
    return make_tool_spec("fs_chmod", "Replace the permission bits of a file or directory using octal notation.", chmod_schema());
}

json call_fs_chmod(const FileToolsContext& context, const json& arguments) {
    const fs::path path = context.resolve_path(require_string(arguments, "path"), true);
    fs::permissions(path, octal_to_permissions(require_string(arguments, "permissions")), fs::perm_options::replace);

    return json{
        {"path", relative_string(context.root_path(), path)},
        {"permissions", permissions_to_octal(fs::status(path).permissions())},
    };
}

} // namespace criper
