#include "criper/fs_tools_support.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace criper {

namespace {

constexpr std::size_t kMaxDebugTextLength = 4096U;

[[nodiscard]] std::mutex& debug_log_mutex() {
    static std::mutex mutex;
    return mutex;
}

[[nodiscard]] std::string status_to_type(const fs::file_status status) {
    switch (status.type()) {
    case fs::file_type::regular:
        return "file";
    case fs::file_type::directory:
        return "directory";
    case fs::file_type::symlink:
        return "symlink";
    case fs::file_type::block:
        return "block";
    case fs::file_type::character:
        return "character";
    case fs::file_type::fifo:
        return "fifo";
    case fs::file_type::socket:
        return "socket";
    case fs::file_type::not_found:
        return "not_found";
    case fs::file_type::none:
    case fs::file_type::unknown:
    default:
        return "other";
    }
}

[[nodiscard]] json make_text_content(const std::string& text) {
    return json{
        {"type", "text"},
        {"text", text},
    };
}

void close_fd(const int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

[[nodiscard]] std::string read_from_fd(const int fd) {
    std::array<char, 4096> buffer{};
    std::string content;

    while (true) {
        const auto bytes_read = read(fd, buffer.data(), buffer.size());
        if (bytes_read == 0) {
            break;
        }
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        content.append(buffer.data(), static_cast<std::size_t>(bytes_read));
    }

    return content;
}

void write_all_to_fd(const int fd, const std::string& content) {
    std::size_t written = 0U;
    while (written < content.size()) {
        const auto result = write(fd, content.data() + written, content.size() - written);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        written += static_cast<std::size_t>(result);
    }
}

[[nodiscard]] int wait_for_process(const pid_t pid, const std::chrono::seconds timeout, bool& timed_out) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int status = 0;

    while (true) {
        const pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            return status;
        }
        if (result < 0) {
            throw ToolError(std::string("waitpid failed: ") + std::strerror(errno));
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            timed_out = true;
            kill(pid, SIGKILL);
            if (waitpid(pid, &status, 0) < 0) {
                throw ToolError(std::string("waitpid after timeout failed: ") + std::strerror(errno));
            }
            return status;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}

} // namespace

FileToolsContext::FileToolsContext(fs::path root_path, const bool debug_enabled)
    : root_path_(std::move(root_path))
    , debug_enabled_(debug_enabled) {
}

const fs::path& FileToolsContext::root_path() const noexcept {
    return root_path_;
}

bool FileToolsContext::debug_enabled() const noexcept {
    return debug_enabled_;
}

fs::path FileToolsContext::normalize_client_path(const fs::path& input_path) const {
    if (!input_path.is_absolute()) {
        return input_path;
    }

    const fs::path normalized_absolute = input_path.lexically_normal();
    if (path_has_prefix(root_path_, normalized_absolute)) {
        return normalized_absolute;
    }

    // Some MCP clients send workspace-absolute paths. When they fall outside the
    // configured root syntactically, reinterpret them as root-relative instead
    // of rejecting the request outright.
    return normalized_absolute.relative_path();
}

fs::path FileToolsContext::resolve_path(const std::string& raw_path, const bool must_exist) const {
    const fs::path client_path = raw_path.empty() ? fs::path(".") : fs::path(raw_path);
    const fs::path normalized_path = normalize_client_path(client_path);

    const fs::path combined = normalized_path.is_absolute()
        ? normalized_path
        : (root_path_ / normalized_path).lexically_normal();
    const fs::path resolved = fs::weakly_canonical(combined);

    if (!path_has_prefix(root_path_, resolved)) {
        throw ToolError("path escapes configured root");
    }
    if (must_exist && !fs::exists(resolved)) {
        throw ToolError("path does not exist: " + raw_path);
    }
    return resolved;
}

json FileToolsContext::make_operation_from_arguments(const json& arguments) const {
    json operation = json::object();
    if (arguments.contains("op")) {
        operation["op"] = require_string(arguments, "op");
    } else if (arguments.contains("type")) {
        operation["type"] = require_string(arguments, "type");
    } else {
        operation["op"] = "replace";
    }

    operation["find"] = require_string(arguments, "find");

    if (arguments.contains("replace")) {
        operation["replace"] = optional_string(arguments, "replace");
    }
    if (arguments.contains("content")) {
        operation["content"] = optional_string(arguments, "content");
    }
    if (arguments.contains("all")) {
        operation["all"] = optional_bool(arguments, "all", false);
    }

    return operation;
}

std::string truncate_for_log(const std::string_view text, const std::size_t limit) {
    if (text.size() <= limit) {
        return std::string(text);
    }

    std::ostringstream stream;
    stream << text.substr(0, limit) << "... [truncated " << (text.size() - limit) << " bytes]";
    return stream.str();
}

void debug_log(const bool enabled, const std::string& message) {
    if (!enabled) {
        return;
    }

    const std::lock_guard<std::mutex> guard(debug_log_mutex());
    std::cerr << "[debug] " << message << '\n';
}

bool path_has_prefix(const fs::path& prefix, const fs::path& path) {
    auto prefix_it = prefix.begin();
    auto path_it = path.begin();

    for (; prefix_it != prefix.end(); ++prefix_it, ++path_it) {
        if (path_it == path.end() || *prefix_it != *path_it) {
            return false;
        }
    }

    return true;
}

std::string permissions_to_octal(const fs::perms permissions) {
    unsigned int mode = 0U;

    const auto apply_bit = [&](const fs::perms bit, const unsigned int octal_bit) {
        if ((permissions & bit) != fs::perms::none) {
            mode |= octal_bit;
        }
    };

    apply_bit(fs::perms::owner_read, 0400U);
    apply_bit(fs::perms::owner_write, 0200U);
    apply_bit(fs::perms::owner_exec, 0100U);
    apply_bit(fs::perms::group_read, 0040U);
    apply_bit(fs::perms::group_write, 0020U);
    apply_bit(fs::perms::group_exec, 0010U);
    apply_bit(fs::perms::others_read, 0004U);
    apply_bit(fs::perms::others_write, 0002U);
    apply_bit(fs::perms::others_exec, 0001U);

    std::ostringstream stream;
    stream << std::oct << std::setfill('0') << std::setw(3) << (mode & 0777U);
    return stream.str();
}

fs::perms octal_to_permissions(const std::string& octal) {
    if (octal.empty() || octal.size() > 4U) {
        throw ToolError("permissions must be an octal string like 755");
    }

    unsigned int value = 0U;
    for (const char character : octal) {
        if (character < '0' || character > '7') {
            throw ToolError("permissions must contain only octal digits");
        }
        value = (value * 8U) + static_cast<unsigned int>(character - '0');
    }

    fs::perms permissions = fs::perms::none;
    const auto apply_bit = [&](const unsigned int mask, const fs::perms bit) {
        if ((value & mask) != 0U) {
            permissions |= bit;
        }
    };

    apply_bit(0400U, fs::perms::owner_read);
    apply_bit(0200U, fs::perms::owner_write);
    apply_bit(0100U, fs::perms::owner_exec);
    apply_bit(0040U, fs::perms::group_read);
    apply_bit(0020U, fs::perms::group_write);
    apply_bit(0010U, fs::perms::group_exec);
    apply_bit(0004U, fs::perms::others_read);
    apply_bit(0002U, fs::perms::others_write);
    apply_bit(0001U, fs::perms::others_exec);
    return permissions;
}

const json& require_object(const json& value, const std::string_view label) {
    if (!value.is_object()) {
        throw ToolError(std::string(label) + " must be an object");
    }
    return value;
}

const json& require_array(const json& value, const std::string_view label) {
    if (!value.is_array()) {
        throw ToolError(std::string(label) + " must be an array");
    }
    return value;
}

std::string require_string(const json& object, const std::string_view key) {
    if (!object.contains(key) || !object.at(key).is_string()) {
        throw ToolError("missing string argument: " + std::string(key));
    }
    return object.at(key).get<std::string>();
}

std::string optional_string(const json& object, const std::string_view key, std::string fallback) {
    if (!object.contains(key)) {
        return fallback;
    }
    if (!object.at(key).is_string()) {
        throw ToolError("argument must be a string: " + std::string(key));
    }
    return object.at(key).get<std::string>();
}

bool optional_bool(const json& object, const std::string_view key, const bool fallback) {
    if (!object.contains(key)) {
        return fallback;
    }
    if (!object.at(key).is_boolean()) {
        throw ToolError("argument must be a boolean: " + std::string(key));
    }
    return object.at(key).get<bool>();
}

std::size_t optional_size(const json& object, const std::string_view key, const std::size_t fallback) {
    if (!object.contains(key)) {
        return fallback;
    }

    const json& value = object.at(key);
    if (!value.is_number_integer() || value.get<std::int64_t>() < 0) {
        throw ToolError("argument must be a non-negative integer: " + std::string(key));
    }
    return static_cast<std::size_t>(value.get<std::uint64_t>());
}

std::uint64_t optional_u64(const json& object, const std::string_view key, const std::uint64_t fallback) {
    if (!object.contains(key)) {
        return fallback;
    }

    const json& value = object.at(key);
    if (!value.is_number_integer() || value.get<std::int64_t>() < 0) {
        throw ToolError("argument must be a non-negative integer: " + std::string(key));
    }
    return value.get<std::uint64_t>();
}

std::string relative_string(const fs::path& root_path, const fs::path& path) {
    const fs::path relative = fs::relative(path, root_path);
    const auto text = relative.generic_string();
    return text.empty() ? "." : text;
}

json make_tool_spec(const std::string_view name, const std::string_view description, json input_schema) {
    return json{
        {"name", name},
        {"description", description},
        {"inputSchema", std::move(input_schema)},
    };
}

json base_schema() {
    return json{
        {"type", "object"},
        {"additionalProperties", false},
    };
}

json make_tool_result(json structured) {
    return json{
        {"content", json::array({make_text_content(structured.dump())})},
        {"structuredContent", std::move(structured)},
        {"isError", false},
    };
}

json make_tool_error(std::string message) {
    return json{
        {"content", json::array({make_text_content(message)})},
        {"isError", true},
    };
}

json make_stat_object(const fs::path& root_path, const fs::path& path) {
    const fs::file_status status = fs::symlink_status(path);

    json entry{
        {"path", relative_string(root_path, path)},
        {"type", status_to_type(status)},
        {"permissions", permissions_to_octal(status.permissions())},
        {"exists", fs::exists(status)},
        {"is_symlink", fs::is_symlink(status)},
    };

    if (fs::is_regular_file(status)) {
        entry["size"] = static_cast<std::uint64_t>(fs::file_size(path));
    }

    return entry;
}

std::string lowercase_copy(const std::string_view text) {
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return lowered;
}

bool text_matches_pattern(
    const std::string_view text,
    const std::string_view pattern,
    const bool case_sensitive,
    const std::regex* regex_matcher
) {
    if (regex_matcher != nullptr) {
        return std::regex_search(text.begin(), text.end(), *regex_matcher);
    }

    if (case_sensitive) {
        return text.find(pattern) != std::string_view::npos;
    }

    return lowercase_copy(text).find(lowercase_copy(pattern)) != std::string::npos;
}

void remove_existing_path(const fs::path& path) {
    if (!fs::exists(path)) {
        return;
    }

    if (fs::is_directory(path) && !fs::is_symlink(path)) {
        fs::remove_all(path);
        return;
    }

    fs::remove(path);
}

ExecResult execute_command(
    const fs::path& working_directory,
    const std::vector<std::string>& arguments,
    const std::map<std::string, std::string>& extra_environment,
    const std::string& stdin_text,
    const std::chrono::seconds timeout
) {
    if (arguments.empty()) {
        throw ToolError("argv must contain at least one item");
    }

    int stdin_pipe[2]{-1, -1};
    int stdout_pipe[2]{-1, -1};
    int stderr_pipe[2]{-1, -1};

    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        close_fd(stdin_pipe[0]);
        close_fd(stdin_pipe[1]);
        close_fd(stdout_pipe[0]);
        close_fd(stdout_pipe[1]);
        close_fd(stderr_pipe[0]);
        close_fd(stderr_pipe[1]);
        throw ToolError(std::string("pipe creation failed: ") + std::strerror(errno));
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close_fd(stdin_pipe[0]);
        close_fd(stdin_pipe[1]);
        close_fd(stdout_pipe[0]);
        close_fd(stdout_pipe[1]);
        close_fd(stderr_pipe[0]);
        close_fd(stderr_pipe[1]);
        throw ToolError(std::string("fork failed: ") + std::strerror(errno));
    }

    if (pid == 0) {
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        close_fd(stdin_pipe[0]);
        close_fd(stdin_pipe[1]);
        close_fd(stdout_pipe[0]);
        close_fd(stdout_pipe[1]);
        close_fd(stderr_pipe[0]);
        close_fd(stderr_pipe[1]);

        if (chdir(working_directory.c_str()) != 0) {
            _exit(127);
        }

        for (const auto& [key, value] : extra_environment) {
            setenv(key.c_str(), value.c_str(), 1);
        }

        std::vector<char*> raw_arguments;
        raw_arguments.reserve(arguments.size() + 1U);
        for (const std::string& argument : arguments) {
            raw_arguments.push_back(const_cast<char*>(argument.c_str()));
        }
        raw_arguments.push_back(nullptr);

        execvp(raw_arguments.front(), raw_arguments.data());
        _exit(127);
    }

    close_fd(stdin_pipe[0]);
    close_fd(stdout_pipe[1]);
    close_fd(stderr_pipe[1]);

    std::jthread stdin_writer([&stdin_text, fd = stdin_pipe[1]] {
        write_all_to_fd(fd, stdin_text);
        close_fd(fd);
    });

    std::string stdout_text;
    std::string stderr_text;
    std::jthread stdout_reader([&stdout_text, fd = stdout_pipe[0]] {
        stdout_text = read_from_fd(fd);
        close_fd(fd);
    });
    std::jthread stderr_reader([&stderr_text, fd = stderr_pipe[0]] {
        stderr_text = read_from_fd(fd);
        close_fd(fd);
    });

    ExecResult result;
    const int status = wait_for_process(pid, timeout, result.timed_out);

    stdin_writer.join();
    stdout_reader.join();
    stderr_reader.join();

    result.stdout_text = std::move(stdout_text);
    result.stderr_text = std::move(stderr_text);
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }

    return result;
}

std::size_t replace_single(std::string& content, const std::string& needle, const std::string& replacement) {
    const std::size_t position = content.find(needle);
    if (position == std::string::npos) {
        return 0;
    }
    content.replace(position, needle.size(), replacement);
    return 1;
}

std::size_t replace_all(std::string& content, const std::string& needle, const std::string& replacement) {
    if (needle.empty()) {
        throw ToolError("patch find text must not be empty");
    }

    std::size_t replacements = 0U;
    std::size_t cursor = 0U;
    while ((cursor = content.find(needle, cursor)) != std::string::npos) {
        content.replace(cursor, needle.size(), replacement);
        cursor += replacement.size();
        ++replacements;
    }
    return replacements;
}

std::size_t insert_relative(
    std::string& content,
    const std::string& needle,
    const std::string& payload,
    const bool after
) {
    if (needle.empty()) {
        throw ToolError("patch find text must not be empty");
    }

    const std::size_t position = content.find(needle);
    if (position == std::string::npos) {
        return 0;
    }

    const std::size_t insertion_point = after ? position + needle.size() : position;
    content.insert(insertion_point, payload);
    return 1;
}

std::size_t delete_single(std::string& content, const std::string& needle) {
    if (needle.empty()) {
        throw ToolError("patch find text must not be empty");
    }

    const std::size_t position = content.find(needle);
    if (position == std::string::npos) {
        return 0;
    }
    content.erase(position, needle.size());
    return 1;
}

} // namespace criper
