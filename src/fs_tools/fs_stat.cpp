#include "criper/fs_tool_registry.hpp"

namespace criper {

namespace {

[[nodiscard]] json stat_schema() {
    json schema = base_schema();
    schema["properties"] = {
        {"path", {{"type", "string"}}},
    };
    schema["required"] = json::array({"path"});
    return schema;
}

} // namespace

json make_fs_stat_spec() {
    return make_tool_spec("fs_stat", "Inspect file type, size, and permission bits for a path.", stat_schema());
}

json call_fs_stat(const FileToolsContext& context, const json& arguments) {
    return make_stat_object(context.root_path(), context.resolve_path(require_string(arguments, "path"), true));
}

} // namespace criper
