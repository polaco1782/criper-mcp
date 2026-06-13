#include "criper/sandbox_setup.hpp"

#include "criper/logger.hpp"
#include "criper/sandbox.hpp"

namespace criper {

bool initialize_sandbox(const std::filesystem::path& root_directory, const SandboxMode mode) {
    Sandbox& sandbox = Sandbox::instance();
    const bool initialize_success = sandbox.initialize(root_directory, mode);
    const bool activate_success = initialize_success && sandbox.activate();
    if (!activate_success) {
        LOG_WARN("[Sandbox] Failed to initialize or activate the filesystem sandbox.");
    }
    return activate_success;
}

} // namespace criper
