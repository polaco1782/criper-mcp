#include "criper/fs_tool_registry.hpp"

#include <fstream>

namespace criper {

namespace {

[[nodiscard]] json write_schema() {
    json schema = base_schema();
    schema["properties"] = {
        {"path", {{"type", "string"}, {"description", "File to write."}}},
        {"content", {{"type", "string"}, {"description", "Text content to write."}}},
        {"append", {{"type", "boolean"}, {"description", "Append instead of overwrite."}}},
        {"create_parents", {{"type", "boolean"}, {"description", "Create parent directories if needed."}}},
    };
    schema["required"] = json::array({"path", "content"});
    return schema;
}

} // namespace

json make_fs_write_spec() {
    return make_tool_spec("fs_write", "Write text content to a file under the configured root.", write_schema());
}

json call_fs_write(const FileToolsContext& context, const json& arguments) {
    const fs::path path = context.resolve_path(require_string(arguments, "path"), false);
    const bool append = optional_bool(arguments, "append", false);
    const bool create_parents = optional_bool(arguments, "create_parents", true);
    const std::string content = require_string(arguments, "content");

    if (create_parents) {
        fs::create_directories(path.parent_path());
    }

    std::ofstream stream(
        path,
        std::ios::binary | std::ios::out | (append ? std::ios::app : std::ios::trunc)
    );
    if (!stream) {
        throw ToolError("failed to open file for writing");
    }
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    stream.flush();

    return json{
        {"path", relative_string(context.root_path(), path)},
        {"bytes_written", static_cast<std::uint64_t>(content.size())},
        {"append", append},
    };
}

} // namespace criper
