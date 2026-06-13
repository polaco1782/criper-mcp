#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace criper {

enum class SandboxMode {
    Strict,
    HostTools,
};

[[nodiscard]] std::optional<SandboxMode> sandbox_mode_from_string(std::string_view value);
[[nodiscard]] std::string_view sandbox_mode_name(SandboxMode mode) noexcept;

class Sandbox {
public:
    static Sandbox& instance();

    bool initialize(const std::filesystem::path& root_directory, SandboxMode mode);
    [[nodiscard]] bool activate();

private:
    Sandbox();

    static bool ensure_directory_exists(const std::filesystem::path& path);
    [[nodiscard]] std::filesystem::path normalize_path(const std::filesystem::path& path) const;

    std::filesystem::path root_directory_;
    SandboxMode mode_;
    bool active_;
    bool supported_;
};

} // namespace criper
