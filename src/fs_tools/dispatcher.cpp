#include "criper/fs_tools.hpp"

#include "criper/fs_tool_registry.hpp"

namespace criper {

FileTools::FileTools(std::filesystem::path root_path, const bool debug_enabled, const bool verbose_enabled)
    : context_(std::move(root_path), debug_enabled, verbose_enabled) {
}

json FileTools::list_tools() const {
    return json::array({
        make_fs_list_spec(),
        make_fs_find_spec(),
        make_fs_read_spec(),
        make_fs_tail_spec(),
        make_fs_write_spec(),
        make_fs_patch_spec(),
        make_fs_grep_spec(),
        make_fs_exec_spec(),
        make_fs_stat_spec(),
        make_fs_chmod_spec(),
        make_fs_mkdir_spec(),
        make_fs_copy_spec(),
        make_fs_move_spec(),
        make_fs_remove_spec(),
    });
}

json FileTools::call(const std::string& tool_name, const json& arguments) const {
    debug_log(
        context_.debug_enabled(),
        "tools/call name=" + tool_name + " arguments=" + truncate_for_log(arguments.dump())
    );

    try {
        json result;
        if (tool_name == "fs_list") {
            result = make_tool_result(call_fs_list(context_, arguments));
        } else if (tool_name == "fs_find") {
            result = make_tool_result(call_fs_find(context_, arguments));
        } else if (tool_name == "fs_read") {
            result = make_tool_result(call_fs_read(context_, arguments));
        } else if (tool_name == "fs_tail") {
            result = make_tool_result(call_fs_tail(context_, arguments));
        } else if (tool_name == "fs_write") {
            result = make_tool_result(call_fs_write(context_, arguments));
        } else if (tool_name == "fs_patch") {
            result = make_tool_result(call_fs_patch(context_, arguments));
        } else if (tool_name == "fs_grep") {
            result = make_tool_result(call_fs_grep(context_, arguments));
        } else if (tool_name == "fs_exec") {
            result = make_tool_result(call_fs_exec(context_, arguments));
        } else if (tool_name == "fs_stat") {
            result = make_tool_result(call_fs_stat(context_, arguments));
        } else if (tool_name == "fs_chmod") {
            result = make_tool_result(call_fs_chmod(context_, arguments));
        } else if (tool_name == "fs_mkdir") {
            result = make_tool_result(call_fs_mkdir(context_, arguments));
        } else if (tool_name == "fs_copy") {
            result = make_tool_result(call_fs_copy(context_, arguments));
        } else if (tool_name == "fs_move") {
            result = make_tool_result(call_fs_move(context_, arguments));
        } else if (tool_name == "fs_remove") {
            result = make_tool_result(call_fs_remove(context_, arguments));
        } else {
            result = make_tool_error("unknown tool: " + tool_name);
        }

        const bool is_error = result.value("isError", false);
        if (context_.verbose_enabled()) {
            std::string status = is_error ? "failed" : "succeeded";
            if (!is_error) {
                if (tool_name == "fs_find" || tool_name == "fs_grep") {
                    const auto& matches = result["structuredContent"]["matches"];
                    status = matches.is_array() && matches.empty() ? "not_found" : "found";
                } else if (tool_name == "fs_list") {
                    const auto& entries = result["structuredContent"]["entries"];
                    status = entries.is_array() && entries.empty() ? "empty" : "found";
                } else if (tool_name == "fs_stat") {
                    status = result["structuredContent"].value("exists", false) ? "found" : "not_found";
                } else if (tool_name == "fs_exec") {
                    const int exit_code = result["structuredContent"].value("exit_code", -1);
                    status = exit_code == 0 ? "succeeded" : "failed";
                } else if (tool_name == "fs_patch") {
                    std::uint64_t matches = 0;
                    const auto& operations = result["structuredContent"].value("operations", json::array());
                    if (operations.is_array()) {
                        for (const auto& operation : operations) {
                            matches += operation.value("matches", static_cast<std::uint64_t>(0));
                        }
                    }
                    status = matches == 0 ? "no_matches" : "matched";
                }
            }
            verbose_log(context_.verbose_enabled(), "tools/call name=" + tool_name + " status=" + status);
        }

        debug_log(
            context_.debug_enabled(),
            "tools/call result name=" + tool_name + " body=" + truncate_for_log(result.dump())
        );
        return result;
    } catch (const ToolError& error) {
        debug_log(context_.debug_enabled(), "tools/call ToolError name=" + tool_name + " message=" + error.what());
        return make_tool_error(error.what());
    } catch (const fs::filesystem_error& error) {
        debug_log(context_.debug_enabled(), "tools/call filesystem_error name=" + tool_name + " message=" + error.what());
        return make_tool_error(error.what());
    } catch (const std::exception& error) {
        debug_log(context_.debug_enabled(), "tools/call exception name=" + tool_name + " message=" + error.what());
        return make_tool_error(error.what());
    }
}

} // namespace criper
