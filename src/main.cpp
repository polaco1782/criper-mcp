#include "mcp_server.hpp"
#include "criper/sandbox_setup.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void print_usage(const char* program_name) {
    std::cerr
        << "Usage: " << program_name
        << " [--root <path>] [--host <address>] [--port <port>] [--sandbox-mode <strict|host-tools>] [--debug]\n"
        << "Environment:\n"
        << "  CRIPER_MCP_ROOT  Root directory exposed by the server.\n"
        << "  CRIPER_MCP_HOST  Bind address. Defaults to 127.0.0.1.\n"
        << "  CRIPER_MCP_PORT  TCP port. Defaults to 9999.\n"
        << "  CRIPER_MCP_SANDBOX_MODE  Sandbox mode: strict or host-tools. Defaults to strict.\n"
        << "  CRIPER_MCP_DEBUG Enable debug logging when set to 1, true, yes, or on.\n";
}

std::string env_or_default(const char* name, std::string fallback) {
    if (const char* value = std::getenv(name)) {
        return value;
    }
    return fallback;
}

std::uint16_t parse_port_or_throw(std::string_view value) {
    const auto parsed = std::stoul(std::string(value));
    if (parsed > 65535U) {
        throw std::out_of_range("port out of range");
    }
    return static_cast<std::uint16_t>(parsed);
}

bool env_bool_or_default(const char* name, const bool fallback) {
    if (const char* value = std::getenv(name)) {
        const std::string lowered = [&] {
            std::string text(value);
            for (char& character : text) {
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            }
            return text;
        }();
        return lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on";
    }
    return fallback;
}

criper::SandboxMode parse_sandbox_mode_or_throw(std::string_view value) {
    if (const auto mode = criper::sandbox_mode_from_string(value)) {
        return *mode;
    }

    throw std::runtime_error("invalid sandbox mode: " + std::string(value));
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::filesystem::path root_path = env_or_default("CRIPER_MCP_ROOT", std::filesystem::current_path().string());
        std::string bind_address = env_or_default("CRIPER_MCP_HOST", "127.0.0.1");
        std::uint16_t port = parse_port_or_throw(env_or_default("CRIPER_MCP_PORT", "9999"));
        criper::SandboxMode sandbox_mode =
            parse_sandbox_mode_or_throw(env_or_default("CRIPER_MCP_SANDBOX_MODE", "strict"));
        bool debug_enabled = env_bool_or_default("CRIPER_MCP_DEBUG", false);

        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];

            if (argument == "--help" || argument == "-h") {
                print_usage(argv[0]);
                return 0;
            }

            if (argument == "--debug") {
                debug_enabled = true;
                continue;
            }

            if (index + 1 >= argc) {
                throw std::runtime_error("missing value for argument: " + argument);
            }

            const std::string value = argv[++index];

            if (argument == "--root") {
                root_path = value;
            } else if (argument == "--host") {
                bind_address = value;
            } else if (argument == "--port") {
                port = parse_port_or_throw(value);
            } else if (argument == "--sandbox-mode") {
                sandbox_mode = parse_sandbox_mode_or_throw(value);
            } else {
                throw std::runtime_error("unknown argument: " + argument);
            }
        }

        std::filesystem::create_directories(root_path);
        root_path = std::filesystem::weakly_canonical(root_path);

        if (!criper::initialize_sandbox(root_path, sandbox_mode)) {
            std::cerr << "Failed to initialize filesystem sandbox. Aborting for safety." << std::endl;
            return 1;
        }

        criper::McpServer server(std::move(root_path), std::move(bind_address), port, debug_enabled);

        std::cout
            << "Serving MCP over HTTP on " << server.bind_address() << ":" << server.port()
            << " with root " << server.root_path() << '\n';
        if (server.debug_enabled()) {
            std::cerr << "[debug] verbose logging enabled\n";
        }
        server.run();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "fatal: " << exception.what() << '\n';
        return 1;
    }
}
