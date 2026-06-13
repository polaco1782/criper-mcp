#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace criper {

class McpServer {
public:
    McpServer(std::filesystem::path root_path, std::string bind_address, std::uint16_t port, bool debug_enabled);

    [[nodiscard]] const std::filesystem::path& root_path() const noexcept;
    [[nodiscard]] const std::string& bind_address() const noexcept;
    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] bool debug_enabled() const noexcept;

    void run() const;

private:
    std::filesystem::path root_path_;
    std::string bind_address_;
    std::uint16_t port_;
    bool debug_enabled_;

    [[nodiscard]] std::optional<std::string> handle_http_request(
        std::string_view method,
        std::string_view target,
        std::string_view body
    ) const;
};

} // namespace criper
