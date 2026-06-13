#include "criper/fs_tool_registry.hpp"

#include <fstream>

namespace criper {

namespace {

[[nodiscard]] json read_schema() {
    json schema = base_schema();
    schema["properties"] = {
        {"path", {{"type", "string"}, {"description", "File to read."}}},
        {"offset", {{"type", "integer"}, {"minimum", 0}, {"description", "Byte offset. Default: 0"}}},
        {"length", {{"type", "integer"}, {"minimum", 1}, {"description", "Maximum bytes to read."}}},
    };
    schema["required"] = json::array({"path"});
    return schema;
}

} // namespace

json make_fs_read_spec() {
    return make_tool_spec("fs_read", "Read a UTF-8 or text-like file from the configured root.", read_schema());
}

json call_fs_read(const FileToolsContext& context, const json& arguments) {
    const fs::path path = context.resolve_path(require_string(arguments, "path"), true);
    if (!fs::is_regular_file(path)) {
        throw ToolError("path is not a regular file");
    }

    const std::uint64_t offset = optional_u64(arguments, "offset", 0U);
    const std::size_t length = optional_size(arguments, "length", 1024U * 1024U);

    std::ifstream stream(path, std::ios::binary);
    stream.seekg(0, std::ios::end);
    const std::uint64_t file_size = static_cast<std::uint64_t>(stream.tellg());
    if (offset > file_size) {
        throw ToolError("offset is beyond the end of the file");
    }

    const std::size_t to_read = static_cast<std::size_t>(std::min<std::uint64_t>(length, file_size - offset));
    std::string content(to_read, '\0');
    stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    stream.read(content.data(), static_cast<std::streamsize>(to_read));
    content.resize(static_cast<std::size_t>(stream.gcount()));

    return json{
        {"path", relative_string(context.root_path(), path)},
        {"offset", offset},
        {"bytes_read", static_cast<std::uint64_t>(content.size())},
        {"content", std::move(content)},
    };
}

} // namespace criper
