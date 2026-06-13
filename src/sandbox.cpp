#include "criper/sandbox.hpp"

#include "criper/logger.hpp"

#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

#ifdef __linux__
#include <fcntl.h>
#include <linux/landlock.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef __NR_landlock_create_ruleset
#define __NR_landlock_create_ruleset 444
#endif
#ifndef __NR_landlock_add_rule
#define __NR_landlock_add_rule 445
#endif
#ifndef __NR_landlock_restrict_self
#define __NR_landlock_restrict_self 446
#endif

#ifndef LANDLOCK_ACCESS_FS_EXECUTE
#define LANDLOCK_ACCESS_FS_EXECUTE          (1ULL << 0)
#define LANDLOCK_ACCESS_FS_WRITE_FILE       (1ULL << 1)
#define LANDLOCK_ACCESS_FS_READ_FILE        (1ULL << 2)
#define LANDLOCK_ACCESS_FS_READ_DIR         (1ULL << 3)
#define LANDLOCK_ACCESS_FS_REMOVE_DIR       (1ULL << 4)
#define LANDLOCK_ACCESS_FS_REMOVE_FILE      (1ULL << 5)
#define LANDLOCK_ACCESS_FS_MAKE_CHAR        (1ULL << 6)
#define LANDLOCK_ACCESS_FS_MAKE_DIR         (1ULL << 7)
#define LANDLOCK_ACCESS_FS_MAKE_REG         (1ULL << 8)
#define LANDLOCK_ACCESS_FS_MAKE_SOCK        (1ULL << 9)
#define LANDLOCK_ACCESS_FS_MAKE_FIFO        (1ULL << 10)
#define LANDLOCK_ACCESS_FS_MAKE_BLOCK       (1ULL << 11)
#define LANDLOCK_ACCESS_FS_MAKE_SYM         (1ULL << 12)

struct landlock_ruleset_attr {
    __u64 handled_access_fs;
};

struct landlock_path_beneath_attr {
    __u64 allowed_access;
    __s32 parent_fd;
} __attribute__((packed));
#endif

#ifndef LANDLOCK_RULE_PATH_BENEATH
#define LANDLOCK_RULE_PATH_BENEATH 1
#endif

namespace {

constexpr __u64 kLandlockReadWriteAccess =
    LANDLOCK_ACCESS_FS_EXECUTE |
    LANDLOCK_ACCESS_FS_WRITE_FILE |
    LANDLOCK_ACCESS_FS_READ_FILE |
    LANDLOCK_ACCESS_FS_READ_DIR |
    LANDLOCK_ACCESS_FS_REMOVE_DIR |
    LANDLOCK_ACCESS_FS_REMOVE_FILE |
    LANDLOCK_ACCESS_FS_MAKE_CHAR |
    LANDLOCK_ACCESS_FS_MAKE_DIR |
    LANDLOCK_ACCESS_FS_MAKE_REG |
    LANDLOCK_ACCESS_FS_MAKE_SOCK |
    LANDLOCK_ACCESS_FS_MAKE_FIFO |
    LANDLOCK_ACCESS_FS_MAKE_BLOCK |
    LANDLOCK_ACCESS_FS_MAKE_SYM;

constexpr __u64 kLandlockReadOnlyAccess =
    LANDLOCK_ACCESS_FS_EXECUTE |
    LANDLOCK_ACCESS_FS_READ_FILE |
    LANDLOCK_ACCESS_FS_READ_DIR;

int landlock_create_ruleset(const struct landlock_ruleset_attr* attribute, const std::size_t size, const __u32 flags) {
    return static_cast<int>(syscall(__NR_landlock_create_ruleset, attribute, size, flags));
}

int landlock_add_rule(const int ruleset_fd, const int type, const void* attribute, const __u32 flags) {
    return static_cast<int>(syscall(__NR_landlock_add_rule, ruleset_fd, type, attribute, flags));
}

int landlock_restrict_self(const int ruleset_fd, const __u32 flags) {
    return static_cast<int>(syscall(__NR_landlock_restrict_self, ruleset_fd, flags));
}

} // namespace
#endif

namespace criper {

namespace {

namespace fs = std::filesystem;

constexpr std::array<std::string_view, 8> kHostToolReadOnlyDirectories{
    "/bin",
    "/sbin",
    "/usr/bin",
    "/usr/sbin",
    "/lib",
    "/lib64",
    "/usr/lib",
    "/usr/lib64",
};

} // namespace

std::optional<SandboxMode> sandbox_mode_from_string(std::string_view value) {
    std::string lowered(value);
    for (char& character : lowered) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    if (lowered == "strict") {
        return SandboxMode::Strict;
    }
    if (lowered == "host-tools") {
        return SandboxMode::HostTools;
    }
    return std::nullopt;
}

std::string_view sandbox_mode_name(const SandboxMode mode) noexcept {
    switch (mode) {
    case SandboxMode::Strict:
        return "strict";
    case SandboxMode::HostTools:
        return "host-tools";
    }

    return "unknown";
}

Sandbox& Sandbox::instance() {
    static Sandbox sandbox;
    return sandbox;
}

Sandbox::Sandbox()
    : mode_(SandboxMode::Strict)
    , active_(false)
    , supported_(false) {
#ifdef __linux__
    landlock_ruleset_attr attribute{};
    attribute.handled_access_fs = kLandlockReadWriteAccess;

    const int ruleset_fd = landlock_create_ruleset(&attribute, sizeof(attribute), 0);
    if (ruleset_fd >= 0) {
        supported_ = true;
        close(ruleset_fd);
    } else {
        supported_ = (errno != ENOSYS && errno != EOPNOTSUPP);
    }
#endif
}

bool Sandbox::ensure_directory_exists(const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        return std::filesystem::is_directory(path, error);
    }

    if (error) {
        return false;
    }

    return std::filesystem::create_directories(path, error) || !error;
}

std::filesystem::path Sandbox::normalize_path(const std::filesystem::path& path) const {
    std::error_code error;
    std::filesystem::path candidate = path;
    if (!candidate.is_absolute()) {
        candidate = std::filesystem::current_path(error) / candidate;
        if (error) {
            return path.lexically_normal();
        }
    }

    candidate = candidate.lexically_normal();
    const std::filesystem::path normalized = std::filesystem::weakly_canonical(candidate, error);
    return error ? candidate : normalized;
}

bool Sandbox::initialize(const std::filesystem::path& root_directory, const SandboxMode mode) {
    if (active_) {
        LOG_WARN("[Sandbox] Sandbox is already active.");
        return false;
    }

    const std::filesystem::path requested_root =
        root_directory.empty() ? std::filesystem::current_path() : root_directory;

    if (!ensure_directory_exists(requested_root)) {
        LOG_ERROR("[Sandbox] Failed to create root directory: %s", requested_root.c_str());
        return false;
    }

    root_directory_ = normalize_path(requested_root);
    mode_ = mode;

    LOG_INFO("[Sandbox] Landlock supported: %s", supported_ ? "yes" : "no");
    LOG_INFO("[Sandbox] Root directory initialized: %s", root_directory_.c_str());
    LOG_INFO("[Sandbox] Mode: %.*s", static_cast<int>(sandbox_mode_name(mode_).size()), sandbox_mode_name(mode_).data());

    return true;
}

bool Sandbox::activate() {
    if (active_) {
        return true;
    }

#ifndef __linux__
    LOG_WARN("[Sandbox] Landlock is only available on Linux. Sandbox not active.");
    return false;
#else
    if (!supported_) {
        LOG_WARN("[Sandbox] Landlock is not supported by this kernel. Sandbox not active.");
        LOG_WARN("[Sandbox] Upgrade to Linux 5.13 or newer for filesystem sandboxing.");
        return false;
    }

    landlock_ruleset_attr ruleset_attribute{};
    ruleset_attribute.handled_access_fs = kLandlockReadWriteAccess;

    const int ruleset_fd = landlock_create_ruleset(&ruleset_attribute, sizeof(ruleset_attribute), 0);
    if (ruleset_fd < 0) {
        LOG_ERROR("[Sandbox] Failed to create Landlock ruleset: %s", std::strerror(errno));
        return false;
    }

    const auto add_rule = [&](const std::filesystem::path& directory, const __u64 access_mask) -> bool {
        const int directory_fd = open(directory.c_str(), O_PATH | O_CLOEXEC);
        if (directory_fd < 0) {
            LOG_ERROR(
                "[Sandbox] Failed to open '%s' for a Landlock rule: %s",
                directory.c_str(),
                std::strerror(errno)
            );
            return false;
        }

        landlock_path_beneath_attr path_attribute{};
        path_attribute.allowed_access = access_mask;
        path_attribute.parent_fd = directory_fd;

        const int result = landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH, &path_attribute, 0);
        close(directory_fd);

        if (result < 0) {
            LOG_ERROR(
                "[Sandbox] Failed to add a Landlock rule for '%s': %s",
                directory.c_str(),
                std::strerror(errno)
            );
            return false;
        }

        return true;
    };

    // Keep the sandbox policy intentionally minimal: only the configured root is visible.
    if (!add_rule(root_directory_, kLandlockReadWriteAccess)) {
        close(ruleset_fd);
        return false;
    }
    LOG_DEBUG("[Sandbox] Allowed R/W: %s", root_directory_.c_str());

    if (mode_ == SandboxMode::HostTools) {
        // Host-tools mode keeps writes confined to the configured root while
        // exposing a read-only runtime surface for host executables and loaders
        // without also exposing unrelated host configuration trees.
        for (const std::string_view directory : kHostToolReadOnlyDirectories) {
            if (!add_rule(fs::path(directory), kLandlockReadOnlyAccess)) {
                close(ruleset_fd);
                return false;
            }
            LOG_DEBUG("[Sandbox] Allowed R/O: %.*s", static_cast<int>(directory.size()), directory.data());
        }
    }

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        LOG_ERROR("[Sandbox] Failed to set no_new_privs: %s", std::strerror(errno));
        close(ruleset_fd);
        return false;
    }

    if (landlock_restrict_self(ruleset_fd, 0) < 0) {
        LOG_ERROR("[Sandbox] Failed to restrict the current process: %s", std::strerror(errno));
        close(ruleset_fd);
        return false;
    }

    close(ruleset_fd);
    active_ = true;

    LOG_INFO("[Sandbox] ===== LANDLOCK SANDBOX ACTIVE =====");
    LOG_INFO(
        "[Sandbox] Process and children restricted with %.*s mode.",
        static_cast<int>(sandbox_mode_name(mode_).size()),
        sandbox_mode_name(mode_).data()
    );
    LOG_INFO("[Sandbox] Writable root: %s", root_directory_.c_str());

    return true;
#endif
}

} // namespace criper
