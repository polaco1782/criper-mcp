#pragma once

#include "criper/fs_tools_support.hpp"

namespace criper {

[[nodiscard]] json make_fs_list_spec();
[[nodiscard]] json call_fs_list(const FileToolsContext& context, const json& arguments);

[[nodiscard]] json make_fs_find_spec();
[[nodiscard]] json call_fs_find(const FileToolsContext& context, const json& arguments);

[[nodiscard]] json make_fs_read_spec();
[[nodiscard]] json call_fs_read(const FileToolsContext& context, const json& arguments);

[[nodiscard]] json make_fs_tail_spec();
[[nodiscard]] json call_fs_tail(const FileToolsContext& context, const json& arguments);

[[nodiscard]] json make_fs_write_spec();
[[nodiscard]] json call_fs_write(const FileToolsContext& context, const json& arguments);

[[nodiscard]] json make_fs_patch_spec();
[[nodiscard]] json call_fs_patch(const FileToolsContext& context, const json& arguments);

[[nodiscard]] json make_fs_grep_spec();
[[nodiscard]] json call_fs_grep(const FileToolsContext& context, const json& arguments);

[[nodiscard]] json make_fs_exec_spec();
[[nodiscard]] json call_fs_exec(const FileToolsContext& context, const json& arguments);

[[nodiscard]] json make_fs_stat_spec();
[[nodiscard]] json call_fs_stat(const FileToolsContext& context, const json& arguments);

[[nodiscard]] json make_fs_chmod_spec();
[[nodiscard]] json call_fs_chmod(const FileToolsContext& context, const json& arguments);

[[nodiscard]] json make_fs_mkdir_spec();
[[nodiscard]] json call_fs_mkdir(const FileToolsContext& context, const json& arguments);

[[nodiscard]] json make_fs_copy_spec();
[[nodiscard]] json call_fs_copy(const FileToolsContext& context, const json& arguments);

[[nodiscard]] json make_fs_move_spec();
[[nodiscard]] json call_fs_move(const FileToolsContext& context, const json& arguments);

[[nodiscard]] json make_fs_remove_spec();
[[nodiscard]] json call_fs_remove(const FileToolsContext& context, const json& arguments);

[[nodiscard]] json make_git_spec();
[[nodiscard]] json call_git(const FileToolsContext& context, const json& arguments);
[[nodiscard]] json redact_git_arguments(json arguments);

} // namespace criper
