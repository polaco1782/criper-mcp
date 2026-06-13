#pragma once

#include <filesystem>
#include <string>

#include "criper/fs_tools_support.hpp"

namespace criper {

class FileTools {
public:
    explicit FileTools(std::filesystem::path root_path, bool debug_enabled);

    [[nodiscard]] json list_tools() const;
    [[nodiscard]] json call(const std::string& tool_name, const json& arguments) const;

private:
    FileToolsContext context_;
};

} // namespace criper
