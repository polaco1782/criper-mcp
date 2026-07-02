#include "criper/fs_tool_registry.hpp"

#include <fstream>
#include <sstream>

namespace criper {

namespace {

[[nodiscard]] json patch_schema() {
    json schema = base_schema();
    const json operation_kind_schema = {
        {"type", "string"},
        {"enum", json::array({"replace", "insert_before", "insert_after", "delete"})},
        {"description", "Operation: replace (swap text), insert_before (add text before match), insert_after (add text after match), or delete (remove text)."},
    };
    schema["properties"] = {
        {"path", {{"type", "string"}, {"description", "File to patch."}}},
        {"op", operation_kind_schema},
        {"type", {
            {"type", "string"},
            {"description", "Deprecated alias for op."},
        }},
        {"find", {
            {"type", "string"},
            {"description", "Literal text to find (exact string match, NOT regex). Must be a non-empty substring that exists in the file."},
        }},
        {"replace", {
            {"type", "string"},
            {"description", "Literal replacement text (for replace operations only). Use empty string to delete."},
        }},
        {"content", {
            {"type", "string"},
            {"description", "Literal text to insert (for insert_before or insert_after operations only)."},
        }},
        {"all", {
            {"type", "boolean"},
            {"description", "If true, apply operation to ALL matches in file. If false (default), apply to first match only."},
        }},
        {"operations", {
            {"type", "array"},
            {"description", "For multiple edits: array of operations applied in order. Use this instead of simple form when you need multiple changes."},
            {"items", {
                {"type", "object"},
                {"additionalProperties", false},
                {"properties", {
                    {"op", operation_kind_schema},
                    {"type", {
                        {"type", "string"},
                        {"description", "Deprecated alias for op."},
                    }},
                    {"find", {
                        {"type", "string"},
                        {"description", "Literal text to find (exact string match, NOT regex)."},
                    }},
                    {"replace", {
                        {"type", "string"},
                        {"description", "Literal replacement text for replace operations."},
                    }},
                    {"content", {
                        {"type", "string"},
                        {"description", "Literal text to insert for insert_before or insert_after."},
                    }},
                    {"all", {
                        {"type", "boolean"},
                        {"description", "If true, apply to all matches. If false (default), apply to first match only."},
                    }},
                }},
                {"required", json::array({"find"})},
                {"anyOf", json::array({
                    json{{"required", json::array({"op"})}},
                    json{{"required", json::array({"type"})}},
                })},
            }},
        }},
    };
    schema["required"] = json::array({"path"});
    schema["anyOf"] = json::array({
        json{{"required", json::array({"operations"})}},
        json{{"required", json::array({"find"})}},
    });
    return schema;
}

} // namespace

json make_fs_patch_spec() {
    return make_tool_spec(
        "fs_patch",
        "Edit files using literal text replacement (finds exact text matches, NOT regex patterns). Supports replace, insert_before, insert_after, and delete operations.",
        patch_schema()
    );
}

json call_fs_patch(const FileToolsContext& context, const json& arguments) {
    const fs::path path = context.resolve_path(require_string(arguments, "path"), true);
    if (!fs::is_regular_file(path)) {
        throw ToolError("path is not a regular file");
    }

    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    std::string content = buffer.str();

    const json operations = arguments.contains("operations")
        ? require_array(arguments.at("operations"), "operations")
        : json::array({context.make_operation_from_arguments(arguments)});
    json applied = json::array();

    for (const json& operation : operations) {
        const json& validated_operation = require_object(operation, "operation");

        const std::string type = validated_operation.contains("op")
            ? require_string(validated_operation, "op")
            : require_string(validated_operation, "type");
        const std::string find_text = require_string(validated_operation, "find");
        const bool apply_all = optional_bool(validated_operation, "all", false);

        std::size_t matches = 0U;
        if (type == "replace") {
            const std::string replacement = optional_string(validated_operation, "replace");
            matches = apply_all ? replace_all(content, find_text, replacement) : replace_single(content, find_text, replacement);
        } else if (type == "insert_before") {
            matches = insert_relative(content, find_text, optional_string(validated_operation, "content"), false);
        } else if (type == "insert_after") {
            matches = insert_relative(content, find_text, optional_string(validated_operation, "content"), true);
        } else if (type == "delete") {
            matches = apply_all ? replace_all(content, find_text, "") : delete_single(content, find_text);
        } else {
            throw ToolError("unsupported patch operation: " + type);
        }

        if (matches == 0U) {
            throw ToolError("patch operation did not match any text for operation: " + type);
        }

        applied.push_back({
            {"type", type},
            {"matches", static_cast<std::uint64_t>(matches)},
        });
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw ToolError("failed to open file for writing");
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.flush();
    if (!output) {
        throw ToolError("failed to write patched file");
    }
    output.close();
    if (!output) {
        throw ToolError("failed to close patched file");
    }

    std::ifstream verification_input(path, std::ios::binary);
    if (!verification_input) {
        throw ToolError("failed to open patched file for verification");
    }
    std::ostringstream verification_buffer;
    verification_buffer << verification_input.rdbuf();
    if (verification_buffer.str() != content) {
        throw ToolError("patched file verification failed");
    }

    return json{
        {"path", relative_string(context.root_path(), path)},
        {"operations", std::move(applied)},
    };
}

} // namespace criper
