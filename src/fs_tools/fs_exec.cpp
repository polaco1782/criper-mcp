#include "criper/fs_tool_registry.hpp"

namespace criper {

namespace {

[[nodiscard]] json exec_schema() {
    json schema = base_schema();
    schema["properties"] = {
        {"argv", {
            {"type", "array"},
            {"items", {{"type", "string"}}},
        }},
        {"cwd", {{"type", "string"}}},
        {"stdin", {{"type", "string"}}},
        {"timeout_secs", {{"type", "integer"}, {"minimum", 1}}},
        {"env", {{"type", "object"}}},
    };
    schema["required"] = json::array({"argv"});
    return schema;
}

} // namespace

json make_fs_exec_spec() {
    return make_tool_spec("fs_exec", "Execute a command inside the configured root and capture stdout and stderr.", exec_schema());
}

json call_fs_exec(const FileToolsContext& context, const json& arguments) {
    const json& argv_values = require_array(arguments.at("argv"), "argv");
    std::vector<std::string> argv;
    argv.reserve(argv_values.size());
    for (const json& value : argv_values) {
        if (!value.is_string()) {
            throw ToolError("argv entries must be strings");
        }
        argv.push_back(value.get<std::string>());
    }

    const fs::path working_directory = context.resolve_path(optional_string(arguments, "cwd", "."), true);
    if (!fs::is_directory(working_directory)) {
        throw ToolError("cwd is not a directory");
    }

    std::map<std::string, std::string> environment;
    if (arguments.contains("env")) {
        const json& env_object = require_object(arguments.at("env"), "env");
        for (auto it = env_object.begin(); it != env_object.end(); ++it) {
            if (!it.value().is_string()) {
                throw ToolError("environment values must be strings");
            }
            environment.emplace(it.key(), it.value().get<std::string>());
        }
    }

    const ExecResult execution = execute_command(
        working_directory,
        argv,
        environment,
        optional_string(arguments, "stdin"),
        std::chrono::seconds(optional_u64(arguments, "timeout_secs", 30U))
    );

    debug_log(
        context.debug_enabled(),
        "fs_exec cwd=" + relative_string(context.root_path(), working_directory)
            + " argv=" + truncate_for_log(json(argv).dump())
            + " exit_code=" + std::to_string(execution.exit_code)
            + " timed_out=" + std::string(execution.timed_out ? "true" : "false")
            + " stdout=" + truncate_for_log(execution.stdout_text)
            + " stderr=" + truncate_for_log(execution.stderr_text)
    );

    return json{
        {"cwd", relative_string(context.root_path(), working_directory)},
        {"argv", argv},
        {"exit_code", execution.exit_code},
        {"timed_out", execution.timed_out},
        {"stdout", execution.stdout_text},
        {"stderr", execution.stderr_text},
    };
}

} // namespace criper
