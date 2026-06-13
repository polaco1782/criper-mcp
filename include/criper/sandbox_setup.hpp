#pragma once

#include "criper/sandbox.hpp"

#include <filesystem>

namespace criper {

[[nodiscard]] bool initialize_sandbox(const std::filesystem::path& root_directory, SandboxMode mode);

} // namespace criper
