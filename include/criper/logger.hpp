#pragma once

#include <cstdarg>
#include <cstdio>

namespace criper {

inline void log(const char* level, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    std::fprintf(stderr, "[%s] ", level);
    std::vfprintf(stderr, format, arguments);
    std::fprintf(stderr, "\n");
    va_end(arguments);
}

} // namespace criper

#define LOG_ERROR(...) ::criper::log("ERROR", __VA_ARGS__)
#define LOG_WARN(...)  ::criper::log("WARN", __VA_ARGS__)
#define LOG_DEBUG(...) ::criper::log("DEBUG", __VA_ARGS__)
#define LOG_INFO(...)  ::criper::log("INFO", __VA_ARGS__)
