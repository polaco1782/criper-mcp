#include "criper/fs_tool_registry.hpp"
#include "criper/sandbox.hpp"

#include <git2.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>

namespace criper {

namespace {

template <typename T, void (*FreeFn)(T*)>
using git_ptr = std::unique_ptr<T, decltype(FreeFn)>;

struct Libgit2Runtime {
    Libgit2Runtime() {
        git_libgit2_init();
    }

    ~Libgit2Runtime() {
        git_libgit2_shutdown();
    }
};

void ensure_libgit2_initialized() {
    static Libgit2Runtime runtime;
    (void)runtime;
}

std::mutex& worktree_add_mutex() {
    static std::mutex mutex;
    return mutex;
}

[[nodiscard]] std::string last_git_error() {
    if (const git_error* error = git_error_last()) {
        if (error->message != nullptr) {
            return error->message;
        }
    }
    return "libgit2 operation failed";
}

void check_git(const int result, const std::string_view action) {
    if (result < 0) {
        throw ToolError(std::string(action) + ": " + last_git_error());
    }
}

void configure_libgit2_sandbox_paths(const FileToolsContext& context) {
    ensure_libgit2_initialized();

    const Sandbox& sandbox = Sandbox::instance();
    if (!sandbox.active()) {
        return;
    }

    const fs::path root = sandbox.root_directory().empty() ? context.root_path() : sandbox.root_directory();
    const fs::path base = root / ".criper-mcp" / "libgit2";
    const fs::path home = base / "home";
    const fs::path ssh = home / ".ssh";
    const fs::path system = base / "system";
    const fs::path xdg = base / "xdg";
    const fs::path programdata = base / "programdata";
    const fs::path templates = base / "templates";
    const fs::path certs = base / "certs";

    std::error_code error;
    fs::create_directories(home, error);
    if (error) {
        throw ToolError("prepare sandboxed libgit2 home: " + error.message());
    }
    fs::create_directories(ssh, error);
    if (error) {
        throw ToolError("prepare sandboxed libgit2 ssh path: " + error.message());
    }
    fs::create_directories(system, error);
    if (error) {
        throw ToolError("prepare sandboxed libgit2 system config path: " + error.message());
    }
    fs::create_directories(xdg, error);
    if (error) {
        throw ToolError("prepare sandboxed libgit2 xdg config path: " + error.message());
    }
    fs::create_directories(programdata, error);
    if (error) {
        throw ToolError("prepare sandboxed libgit2 programdata config path: " + error.message());
    }
    fs::create_directories(templates, error);
    if (error) {
        throw ToolError("prepare sandboxed libgit2 template path: " + error.message());
    }
    fs::create_directories(certs, error);
    if (error) {
        throw ToolError("prepare sandboxed libgit2 certificate path: " + error.message());
    }

    check_git(git_libgit2_opts(GIT_OPT_SET_HOMEDIR, home.string().c_str()), "sandbox libgit2 home");
    check_git(git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_GLOBAL, home.string().c_str()), "sandbox libgit2 global config");
    check_git(git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_SYSTEM, system.string().c_str()), "sandbox libgit2 system config");
    check_git(git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_XDG, xdg.string().c_str()), "sandbox libgit2 xdg config");
    check_git(
        git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_PROGRAMDATA, programdata.string().c_str()),
        "sandbox libgit2 programdata config"
    );
    check_git(git_libgit2_opts(GIT_OPT_SET_TEMPLATE_PATH, templates.string().c_str()), "sandbox libgit2 template path");
    check_git(git_libgit2_opts(GIT_OPT_SET_SSL_CERT_LOCATIONS, nullptr, certs.string().c_str()), "sandbox libgit2 certificate path");
    check_git(git_libgit2_opts(GIT_OPT_SET_OWNER_VALIDATION, 0), "sandbox libgit2 owner validation");
}

[[nodiscard]] json git_schema() {
    json schema = base_schema();
    const json credentials_schema = {
        {"type", "object"},
        {"additionalProperties", false},
        {"description", "Per-call credentials for clone, fetch, pull, and push. Credentials are not persisted."},
        {"properties", {
            {"username", {
                {"type", "string"},
                {"description", "Username for HTTPS auth or SSH auth. Defaults to username from URL, or git for SSH."},
            }},
            {"password", {
                {"type", "string"},
                {"description", "HTTPS password or personal access token secret."},
            }},
            {"token", {
                {"type", "string"},
                {"description", "HTTPS token. Used as the secret with username x-access-token when username is omitted."},
            }},
            {"use_ssh_agent", {
                {"type", "boolean"},
                {"description", "Use the local SSH agent for SSH authentication."},
                {"default", false},
            }},
            {"ssh_public_key_path", {
                {"type", "string"},
                {"description", "Path to the SSH public key, relative to the MCP root. Optional when libssh2 can derive it."},
            }},
            {"ssh_private_key_path", {
                {"type", "string"},
                {"description", "Path to the SSH private key, relative to the MCP root."},
            }},
            {"ssh_passphrase", {
                {"type", "string"},
                {"description", "Passphrase for the SSH private key."},
            }},
        }},
    };

    schema["properties"] = {
        {"op", {
            {"type", "string"},
            {"description", "Git operation to run."},
            {"enum", json::array({
                "status",
                "diff",
                "log",
                "head",
                "branches",
                "remotes",
                "init",
                "add",
                "commit",
                "checkout",
                "branch_create",
                "branch_delete",
                "reset",
                "merge",
                "clone",
                "fetch",
                "pull",
                "push",
                "worktree_add",
                "worktree_list",
            })},
        }},
        {"path", {
            {"type", "string"},
            {"description", "Repository path relative to the MCP root. Defaults to ."},
            {"default", "."},
        }},
        {"url", {
            {"type", "string"},
            {"description", "Remote repository URL. Required for clone."},
        }},
        {"remote", {
            {"type", "string"},
            {"description", "Remote name for fetch, pull, or push. Defaults to origin."},
            {"default", "origin"},
        }},
        {"ref", {
            {"type", "string"},
            {"description", "Revision, commit, tag, or ref name for checkout, branch_create, or reset. Defaults vary by operation."},
        }},
        {"branch", {
            {"type", "string"},
            {"description", "Branch name for checkout, clone checkout branch, merge source branch, or new worktree branch."},
        }},
        {"name", {
            {"type", "string"},
            {"description", "Branch name for branch_create or branch_delete, or worktree administrative name."},
        }},
        {"worktree_path", {
            {"type", "string"},
            {"description", "Target path for worktree_add, relative to the MCP root."},
        }},
        {"message", {
            {"type", "string"},
            {"description", "Commit message. Required for commit."},
        }},
        {"author_name", {
            {"type", "string"},
            {"description", "Commit author name. Defaults to criper-mcp."},
        }},
        {"author_email", {
            {"type", "string"},
            {"description", "Commit author email. Defaults to criper-mcp@example.invalid."},
        }},
        {"committer_name", {
            {"type", "string"},
            {"description", "Commit committer name. Defaults to criper-mcp."},
        }},
        {"committer_email", {
            {"type", "string"},
            {"description", "Commit committer email. Defaults to criper-mcp@example.invalid."},
        }},
        {"mode", {
            {"type", "string"},
            {"description", "Reset mode for reset."},
            {"enum", json::array({"soft", "mixed", "hard"})},
            {"default", "mixed"},
        }},
        {"staged", {
            {"type", "boolean"},
            {"description", "For diff, show staged changes instead of worktree changes."},
            {"default", false},
        }},
        {"force", {
            {"type", "boolean"},
            {"description", "Required for destructive variants: hard reset, force checkout, branch_delete, and force push refspecs."},
            {"default", false},
        }},
        {"checkout_existing", {
            {"type", "boolean"},
            {"description", "For worktree_add, allow checking out an existing local branch that matches the worktree name."},
            {"default", false},
        }},
        {"lock", {
            {"type", "boolean"},
            {"description", "For worktree_add, lock the newly-created worktree."},
            {"default", false},
        }},
        {"bare", {
            {"type", "boolean"},
            {"description", "Create a bare repository for init or clone."},
            {"default", false},
        }},
        {"paths", {
            {"type", "array"},
            {"description", "Pathspecs for add. Defaults to [\".\"]."},
            {"items", {{"type", "string"}}},
        }},
        {"refspecs", {
            {"type", "array"},
            {"description", "Refspecs for fetch or push. Push requires at least one refspec."},
            {"items", {{"type", "string"}}},
        }},
        {"max_count", {
            {"type", "integer"},
            {"description", "Maximum commits to return from log."},
            {"minimum", 1},
            {"default", 20},
        }},
        {"credentials", credentials_schema},
    };
    schema["required"] = json::array({"op"});
    return schema;
}

[[nodiscard]] fs::path git_path(const FileToolsContext& context, const json& arguments, const bool must_exist) {
    return context.resolve_path(optional_string(arguments, "path", "."), must_exist);
}

[[nodiscard]] git_ptr<git_repository, git_repository_free> open_repository(
    const FileToolsContext& context,
    const json& arguments
) {
    ensure_libgit2_initialized();
    git_repository* raw = nullptr;
    const fs::path path = git_path(context, arguments, true);
    check_git(git_repository_open_ext(&raw, path.string().c_str(), 0, nullptr), "open repository");
    return {raw, git_repository_free};
}

[[nodiscard]] std::vector<std::string> string_array(const json& object, const std::string_view key) {
    std::vector<std::string> values;
    if (!object.contains(key)) {
        return values;
    }

    const json& array = require_array(object.at(key), key);
    values.reserve(array.size());
    for (const json& value : array) {
        if (!value.is_string()) {
            throw ToolError(std::string(key) + " entries must be strings");
        }
        values.push_back(value.get<std::string>());
    }
    return values;
}

[[nodiscard]] git_strarray to_strarray(std::vector<std::string>& values, std::vector<char*>& raw_values) {
    raw_values.clear();
    raw_values.reserve(values.size());
    for (std::string& value : values) {
        raw_values.push_back(value.data());
    }
    return git_strarray{raw_values.data(), raw_values.size()};
}

[[nodiscard]] std::string oid_string(const git_oid* oid) {
    std::array<char, GIT_OID_HEXSZ + 1U> buffer{};
    git_oid_tostr(buffer.data(), buffer.size(), oid);
    return buffer.data();
}

[[nodiscard]] std::string status_text(const git_status_t status) {
    std::string text;
    const auto append = [&text](const std::string_view value) {
        if (!text.empty()) {
            text += ",";
        }
        text += value;
    };

    if ((status & GIT_STATUS_INDEX_NEW) != 0) append("index_new");
    if ((status & GIT_STATUS_INDEX_MODIFIED) != 0) append("index_modified");
    if ((status & GIT_STATUS_INDEX_DELETED) != 0) append("index_deleted");
    if ((status & GIT_STATUS_INDEX_RENAMED) != 0) append("index_renamed");
    if ((status & GIT_STATUS_INDEX_TYPECHANGE) != 0) append("index_typechange");
    if ((status & GIT_STATUS_WT_NEW) != 0) append("worktree_new");
    if ((status & GIT_STATUS_WT_MODIFIED) != 0) append("worktree_modified");
    if ((status & GIT_STATUS_WT_DELETED) != 0) append("worktree_deleted");
    if ((status & GIT_STATUS_WT_TYPECHANGE) != 0) append("worktree_typechange");
    if ((status & GIT_STATUS_WT_RENAMED) != 0) append("worktree_renamed");
    if ((status & GIT_STATUS_WT_UNREADABLE) != 0) append("worktree_unreadable");
    if ((status & GIT_STATUS_IGNORED) != 0) append("ignored");
    return text.empty() ? "current" : text;
}

[[nodiscard]] const char* status_path(const git_status_entry* entry) {
    if (entry->head_to_index != nullptr && entry->head_to_index->new_file.path != nullptr) {
        return entry->head_to_index->new_file.path;
    }
    if (entry->index_to_workdir != nullptr && entry->index_to_workdir->new_file.path != nullptr) {
        return entry->index_to_workdir->new_file.path;
    }
    return "";
}

struct CredentialPayload {
    std::string username;
    std::string password;
    std::string token;
    std::string ssh_public_key_path;
    std::string ssh_private_key_path;
    std::string ssh_passphrase;
    bool use_ssh_agent = false;
};

[[nodiscard]] CredentialPayload credential_payload(const FileToolsContext& context, const json& arguments) {
    CredentialPayload payload;
    if (!arguments.contains("credentials")) {
        return payload;
    }

    const json& credentials = require_object(arguments.at("credentials"), "credentials");
    payload.username = optional_string(credentials, "username");
    payload.password = optional_string(credentials, "password");
    payload.token = optional_string(credentials, "token");
    payload.use_ssh_agent = optional_bool(credentials, "use_ssh_agent", false);

    const std::string public_key_path = optional_string(credentials, "ssh_public_key_path");
    if (!public_key_path.empty()) {
        payload.ssh_public_key_path = context.resolve_path(public_key_path, true).string();
    }

    const std::string private_key_path = optional_string(credentials, "ssh_private_key_path");
    if (!private_key_path.empty()) {
        payload.ssh_private_key_path = context.resolve_path(private_key_path, true).string();
    }

    payload.ssh_passphrase = optional_string(credentials, "ssh_passphrase");
    return payload;
}

int acquire_credentials(
    git_credential** out,
    const char* url,
    const char* username_from_url,
    unsigned int allowed_types,
    void* data
) {
    auto* payload = static_cast<CredentialPayload*>(data);
    const std::string username = !payload->username.empty()
        ? payload->username
        : (username_from_url != nullptr ? username_from_url : "git");

    if ((allowed_types & GIT_CREDENTIAL_SSH_KEY) != 0U) {
        if (payload->use_ssh_agent) {
            return git_credential_ssh_key_from_agent(out, username.c_str());
        }
        if (!payload->ssh_private_key_path.empty()) {
            const char* public_key = payload->ssh_public_key_path.empty() ? nullptr : payload->ssh_public_key_path.c_str();
            const char* passphrase = payload->ssh_passphrase.empty() ? nullptr : payload->ssh_passphrase.c_str();
            return git_credential_ssh_key_new(out, username.c_str(), public_key, payload->ssh_private_key_path.c_str(), passphrase);
        }
    }

    if ((allowed_types & GIT_CREDENTIAL_USERPASS_PLAINTEXT) != 0U) {
        const std::string secret = !payload->password.empty() ? payload->password : payload->token;
        if (!secret.empty()) {
            const std::string user = !payload->username.empty() ? payload->username : (!payload->token.empty() ? "x-access-token" : username);
            return git_credential_userpass_plaintext_new(out, user.c_str(), secret.c_str());
        }
    }

    if ((allowed_types & GIT_CREDENTIAL_USERNAME) != 0U && !username.empty()) {
        return git_credential_username_new(out, username.c_str());
    }

    (void)url;
    return GIT_PASSTHROUGH;
}

void apply_remote_callbacks(git_remote_callbacks& callbacks, CredentialPayload& payload) {
    check_git(git_remote_init_callbacks(&callbacks, GIT_REMOTE_CALLBACKS_VERSION), "initialize remote callbacks");
    callbacks.credentials = acquire_credentials;
    callbacks.payload = &payload;
}

[[nodiscard]] json call_git_init(const FileToolsContext& context, const json& arguments) {
    ensure_libgit2_initialized();
    const fs::path path = git_path(context, arguments, false);
    fs::create_directories(path);

    git_repository* raw = nullptr;
    check_git(git_repository_init(&raw, path.string().c_str(), optional_bool(arguments, "bare", false) ? 1U : 0U), "initialize repository");
    git_ptr<git_repository, git_repository_free> repository(raw, git_repository_free);

    return json{{"path", relative_string(context.root_path(), path)}, {"bare", git_repository_is_bare(repository.get()) != 0}};
}

[[nodiscard]] json call_git_status(const FileToolsContext& context, const json& arguments) {
    auto repository = open_repository(context, arguments);
    git_status_options options = GIT_STATUS_OPTIONS_INIT;
    options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;

    git_status_list* raw = nullptr;
    check_git(git_status_list_new(&raw, repository.get(), &options), "read status");
    git_ptr<git_status_list, git_status_list_free> status_list(raw, git_status_list_free);

    json entries = json::array();
    const std::size_t count = git_status_list_entrycount(status_list.get());
    for (std::size_t index = 0; index < count; ++index) {
        const git_status_entry* entry = git_status_byindex(status_list.get(), index);
        entries.push_back({{"path", status_path(entry)}, {"status", status_text(entry->status)}});
    }

    return json{{"entries", std::move(entries)}, {"clean", count == 0U}};
}

int diff_line_cb(const git_diff_delta*, const git_diff_hunk*, const git_diff_line* line, void* payload) {
    auto* output = static_cast<std::string*>(payload);
    output->append(line->content, line->content_len);
    return 0;
}

[[nodiscard]] json call_git_diff(const FileToolsContext& context, const json& arguments) {
    auto repository = open_repository(context, arguments);
    git_diff_options options = GIT_DIFF_OPTIONS_INIT;

    git_index* raw_index = nullptr;
    check_git(git_repository_index(&raw_index, repository.get()), "open index");
    git_ptr<git_index, git_index_free> index(raw_index, git_index_free);

    git_diff* raw_diff = nullptr;
    if (optional_bool(arguments, "staged", false)) {
        git_object* head = nullptr;
        const int head_result = git_revparse_single(&head, repository.get(), "HEAD^{tree}");
        git_tree* tree = nullptr;
        if (head_result == 0) {
            tree = reinterpret_cast<git_tree*>(head);
        }
        check_git(git_diff_tree_to_index(&raw_diff, repository.get(), tree, index.get(), &options), "create staged diff");
        if (tree != nullptr) {
            git_tree_free(tree);
        }
    } else {
        check_git(git_diff_index_to_workdir(&raw_diff, repository.get(), index.get(), &options), "create worktree diff");
    }
    git_ptr<git_diff, git_diff_free> diff(raw_diff, git_diff_free);

    std::string patch;
    check_git(git_diff_print(diff.get(), GIT_DIFF_FORMAT_PATCH, diff_line_cb, &patch), "format diff");
    return json{{"patch", patch}, {"files_changed", static_cast<std::uint64_t>(git_diff_num_deltas(diff.get()))}};
}

[[nodiscard]] git_ptr<git_signature, git_signature_free> make_signature(
    const json& arguments,
    const std::string_view name_key,
    const std::string_view email_key
) {
    git_signature* raw = nullptr;
    const std::string name = optional_string(arguments, name_key, "criper-mcp");
    const std::string email = optional_string(arguments, email_key, "criper-mcp@example.invalid");
    check_git(git_signature_now(&raw, name.c_str(), email.c_str()), "create signature");
    return {raw, git_signature_free};
}

[[nodiscard]] json call_git_add(const FileToolsContext& context, const json& arguments) {
    auto repository = open_repository(context, arguments);
    git_index* raw = nullptr;
    check_git(git_repository_index(&raw, repository.get()), "open index");
    git_ptr<git_index, git_index_free> index(raw, git_index_free);

    std::vector<std::string> paths = string_array(arguments, "paths");
    if (paths.empty()) {
        paths.push_back(".");
    }

    bool add_all = false;
    for (const std::string& path : paths) {
        if (path == ".") {
            add_all = true;
            break;
        }
    }

    if (add_all) {
        std::vector<char*> raw_paths;
        git_strarray pathspec = to_strarray(paths, raw_paths);
        check_git(git_index_add_all(index.get(), &pathspec, GIT_INDEX_ADD_DEFAULT, nullptr, nullptr), "add paths");
    } else {
        for (const std::string& path : paths) {
            check_git(git_index_add_bypath(index.get(), path.c_str()), "add path");
        }
    }

    check_git(git_index_write(index.get()), "write index");
    return json{{"paths", paths}};
}

[[nodiscard]] json call_git_commit(const FileToolsContext& context, const json& arguments) {
    auto repository = open_repository(context, arguments);
    const std::string message = require_string(arguments, "message");

    git_index* raw_index = nullptr;
    check_git(git_repository_index(&raw_index, repository.get()), "open index");
    git_ptr<git_index, git_index_free> index(raw_index, git_index_free);

    git_oid tree_oid;
    check_git(git_index_write_tree(&tree_oid, index.get()), "write tree");

    git_tree* raw_tree = nullptr;
    check_git(git_tree_lookup(&raw_tree, repository.get(), &tree_oid), "lookup tree");
    git_ptr<git_tree, git_tree_free> tree(raw_tree, git_tree_free);

    auto author = make_signature(arguments, "author_name", "author_email");
    auto committer = make_signature(arguments, "committer_name", "committer_email");

    git_commit* raw_parent = nullptr;
    const int parent_result = git_revparse_single(reinterpret_cast<git_object**>(&raw_parent), repository.get(), "HEAD^{commit}");
    std::array<const git_commit*, 1U> parents{raw_parent};

    git_oid commit_oid;
    if (parent_result == 0) {
        check_git(git_commit_create(
            &commit_oid,
            repository.get(),
            "HEAD",
            author.get(),
            committer.get(),
            nullptr,
            message.c_str(),
            tree.get(),
            1,
            parents.data()
        ), "create commit");
        git_commit_free(raw_parent);
    } else if (parent_result == GIT_ENOTFOUND || parent_result == GIT_EUNBORNBRANCH) {
        check_git(git_commit_create(
            &commit_oid,
            repository.get(),
            "HEAD",
            author.get(),
            committer.get(),
            nullptr,
            message.c_str(),
            tree.get(),
            0,
            nullptr
        ), "create initial commit");
    } else {
        check_git(parent_result, "lookup parent commit");
    }

    return json{{"oid", oid_string(&commit_oid)}, {"message", message}};
}

[[nodiscard]] json call_git_head(const FileToolsContext& context, const json& arguments) {
    auto repository = open_repository(context, arguments);
    git_reference* raw_head = nullptr;
    check_git(git_repository_head(&raw_head, repository.get()), "read HEAD");
    git_ptr<git_reference, git_reference_free> head(raw_head, git_reference_free);

    const git_oid* target = git_reference_target(head.get());
    return json{
        {"name", git_reference_name(head.get())},
        {"shorthand", git_reference_shorthand(head.get())},
        {"oid", target != nullptr ? oid_string(target) : ""},
        {"detached", git_repository_head_detached(repository.get()) != 0},
    };
}

[[nodiscard]] json call_git_log(const FileToolsContext& context, const json& arguments) {
    auto repository = open_repository(context, arguments);
    git_revwalk* raw = nullptr;
    check_git(git_revwalk_new(&raw, repository.get()), "create revwalk");
    git_ptr<git_revwalk, git_revwalk_free> walker(raw, git_revwalk_free);
    check_git(git_revwalk_push_head(walker.get()), "push HEAD");

    const std::uint64_t max_count = optional_u64(arguments, "max_count", 20U);
    json commits = json::array();
    git_oid oid;
    while (commits.size() < max_count && git_revwalk_next(&oid, walker.get()) == 0) {
        git_commit* raw_commit = nullptr;
        check_git(git_commit_lookup(&raw_commit, repository.get(), &oid), "lookup commit");
        git_ptr<git_commit, git_commit_free> commit(raw_commit, git_commit_free);
        const git_signature* author = git_commit_author(commit.get());
        commits.push_back({
            {"oid", oid_string(&oid)},
            {"summary", git_commit_summary(commit.get()) != nullptr ? git_commit_summary(commit.get()) : ""},
            {"message", git_commit_message(commit.get()) != nullptr ? git_commit_message(commit.get()) : ""},
            {"author_name", author != nullptr && author->name != nullptr ? author->name : ""},
            {"author_email", author != nullptr && author->email != nullptr ? author->email : ""},
            {"time", author != nullptr ? static_cast<std::int64_t>(author->when.time) : 0},
        });
    }

    return json{{"commits", std::move(commits)}};
}

[[nodiscard]] json call_git_branches(const FileToolsContext& context, const json& arguments) {
    auto repository = open_repository(context, arguments);
    git_branch_iterator* raw = nullptr;
    check_git(git_branch_iterator_new(&raw, repository.get(), GIT_BRANCH_ALL), "list branches");
    git_ptr<git_branch_iterator, git_branch_iterator_free> iterator(raw, git_branch_iterator_free);

    json branches = json::array();
    git_reference* branch = nullptr;
    git_branch_t type = GIT_BRANCH_LOCAL;
    while (git_branch_next(&branch, &type, iterator.get()) == 0) {
        git_ptr<git_reference, git_reference_free> branch_ref(branch, git_reference_free);
        const char* name = nullptr;
        check_git(git_branch_name(&name, branch_ref.get()), "read branch name");
        branches.push_back({{"name", name != nullptr ? name : ""}, {"remote", type == GIT_BRANCH_REMOTE}});
    }
    return json{{"branches", std::move(branches)}};
}

[[nodiscard]] json call_git_remotes(const FileToolsContext& context, const json& arguments) {
    auto repository = open_repository(context, arguments);
    git_strarray remotes{};
    check_git(git_remote_list(&remotes, repository.get()), "list remotes");

    json entries = json::array();
    for (std::size_t index = 0; index < remotes.count; ++index) {
        git_remote* raw = nullptr;
        check_git(git_remote_lookup(&raw, repository.get(), remotes.strings[index]), "lookup remote");
        git_ptr<git_remote, git_remote_free> remote(raw, git_remote_free);
        entries.push_back({
            {"name", remotes.strings[index]},
            {"url", git_remote_url(remote.get()) != nullptr ? git_remote_url(remote.get()) : ""},
            {"push_url", git_remote_pushurl(remote.get()) != nullptr ? git_remote_pushurl(remote.get()) : ""},
        });
    }
    git_strarray_dispose(&remotes);
    return json{{"remotes", std::move(entries)}};
}

[[nodiscard]] json call_git_checkout(const FileToolsContext& context, const json& arguments) {
    auto repository = open_repository(context, arguments);
    const std::string ref_name = arguments.contains("branch")
        ? "refs/heads/" + require_string(arguments, "branch")
        : require_string(arguments, "ref");

    git_object* raw_target = nullptr;
    check_git(git_revparse_single(&raw_target, repository.get(), ref_name.c_str()), "lookup checkout target");
    git_ptr<git_object, git_object_free> target(raw_target, git_object_free);

    git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
    options.checkout_strategy = optional_bool(arguments, "force", false) ? GIT_CHECKOUT_FORCE : GIT_CHECKOUT_SAFE;
    check_git(git_checkout_tree(repository.get(), target.get(), &options), "checkout tree");

    if (git_object_type(target.get()) == GIT_OBJECT_COMMIT) {
        check_git(git_repository_set_head(repository.get(), ref_name.c_str()), "set HEAD");
    }

    return json{{"ref", ref_name}, {"force", optional_bool(arguments, "force", false)}};
}

[[nodiscard]] json call_git_branch_create(const FileToolsContext& context, const json& arguments) {
    auto repository = open_repository(context, arguments);
    const std::string name = require_string(arguments, "name");
    const std::string start_point = optional_string(arguments, "ref", "HEAD");

    git_object* raw_target = nullptr;
    check_git(git_revparse_single(&raw_target, repository.get(), start_point.c_str()), "lookup branch start point");
    git_ptr<git_object, git_object_free> target_object(raw_target, git_object_free);
    if (git_object_type(target_object.get()) != GIT_OBJECT_COMMIT) {
        throw ToolError("branch start point must resolve to a commit");
    }

    git_reference* raw_branch = nullptr;
    check_git(git_branch_create(
        &raw_branch,
        repository.get(),
        name.c_str(),
        reinterpret_cast<git_commit*>(target_object.get()),
        optional_bool(arguments, "force", false) ? 1 : 0
    ), "create branch");
    git_ptr<git_reference, git_reference_free> branch(raw_branch, git_reference_free);
    return json{{"name", name}, {"ref", git_reference_name(branch.get())}};
}

[[nodiscard]] json call_git_branch_delete(const FileToolsContext& context, const json& arguments) {
    if (!optional_bool(arguments, "force", false)) {
        throw ToolError("branch_delete requires force=true");
    }
    auto repository = open_repository(context, arguments);
    const std::string name = require_string(arguments, "name");

    git_reference* raw_branch = nullptr;
    check_git(git_branch_lookup(&raw_branch, repository.get(), name.c_str(), GIT_BRANCH_LOCAL), "lookup branch");
    git_ptr<git_reference, git_reference_free> branch(raw_branch, git_reference_free);
    check_git(git_branch_delete(branch.get()), "delete branch");
    return json{{"name", name}, {"deleted", true}};
}

[[nodiscard]] json call_git_reset(const FileToolsContext& context, const json& arguments) {
    auto repository = open_repository(context, arguments);
    const std::string mode = optional_string(arguments, "mode", "mixed");
    git_reset_t reset_type = GIT_RESET_MIXED;
    if (mode == "soft") {
        reset_type = GIT_RESET_SOFT;
    } else if (mode == "mixed") {
        reset_type = GIT_RESET_MIXED;
    } else if (mode == "hard") {
        if (!optional_bool(arguments, "force", false)) {
            throw ToolError("hard reset requires force=true");
        }
        reset_type = GIT_RESET_HARD;
    } else {
        throw ToolError("unsupported reset mode: " + mode);
    }

    const std::string target_name = optional_string(arguments, "ref", "HEAD");
    git_object* raw_target = nullptr;
    check_git(git_revparse_single(&raw_target, repository.get(), target_name.c_str()), "lookup reset target");
    git_ptr<git_object, git_object_free> target(raw_target, git_object_free);
    check_git(git_reset(repository.get(), target.get(), reset_type, nullptr), "reset repository");
    return json{{"ref", target_name}, {"mode", mode}};
}

[[nodiscard]] json call_git_merge(const FileToolsContext& context, const json& arguments) {
    auto repository = open_repository(context, arguments);
    const std::string branch_name = require_string(arguments, "branch");

    git_reference* raw_branch = nullptr;
    check_git(git_branch_lookup(&raw_branch, repository.get(), branch_name.c_str(), GIT_BRANCH_LOCAL), "lookup merge branch");
    git_ptr<git_reference, git_reference_free> branch(raw_branch, git_reference_free);

    git_annotated_commit* raw_commit = nullptr;
    check_git(git_annotated_commit_from_ref(&raw_commit, repository.get(), branch.get()), "prepare merge commit");
    git_ptr<git_annotated_commit, git_annotated_commit_free> commit(raw_commit, git_annotated_commit_free);
    const git_annotated_commit* commits[] = {commit.get()};

    git_merge_options merge_options = GIT_MERGE_OPTIONS_INIT;
    git_checkout_options checkout_options = GIT_CHECKOUT_OPTIONS_INIT;
    checkout_options.checkout_strategy = optional_bool(arguments, "force", false) ? GIT_CHECKOUT_FORCE : GIT_CHECKOUT_SAFE;
    check_git(git_merge(repository.get(), commits, 1, &merge_options, &checkout_options), "merge branch");

    git_index* raw_index = nullptr;
    check_git(git_repository_index(&raw_index, repository.get()), "open index");
    git_ptr<git_index, git_index_free> index(raw_index, git_index_free);
    return json{{"branch", branch_name}, {"conflicts", git_index_has_conflicts(index.get()) != 0}};
}

[[nodiscard]] json call_git_clone(const FileToolsContext& context, const json& arguments) {
    ensure_libgit2_initialized();
    const std::string url = require_string(arguments, "url");
    const fs::path path = git_path(context, arguments, false);
    CredentialPayload payload = credential_payload(context, arguments);

    git_clone_options options = GIT_CLONE_OPTIONS_INIT;
    options.bare = optional_bool(arguments, "bare", false) ? 1 : 0;
    const std::string branch = optional_string(arguments, "branch");
    if (!branch.empty()) {
        options.checkout_branch = branch.c_str();
    }
    apply_remote_callbacks(options.fetch_opts.callbacks, payload);

    git_repository* raw = nullptr;
    check_git(git_clone(&raw, url.c_str(), path.string().c_str(), &options), "clone repository");
    git_ptr<git_repository, git_repository_free> repository(raw, git_repository_free);
    return json{{"path", relative_string(context.root_path(), path)}, {"url", url}, {"bare", options.bare != 0}};
}

[[nodiscard]] git_ptr<git_remote, git_remote_free> lookup_remote(git_repository* repository, const json& arguments) {
    git_remote* raw = nullptr;
    const std::string remote_name = optional_string(arguments, "remote", "origin");
    check_git(git_remote_lookup(&raw, repository, remote_name.c_str()), "lookup remote");
    return {raw, git_remote_free};
}

[[nodiscard]] json call_git_fetch(const FileToolsContext& context, const json& arguments) {
    auto repository = open_repository(context, arguments);
    auto remote = lookup_remote(repository.get(), arguments);
    CredentialPayload payload = credential_payload(context, arguments);

    git_fetch_options options = GIT_FETCH_OPTIONS_INIT;
    apply_remote_callbacks(options.callbacks, payload);
    std::vector<std::string> refspec_values = string_array(arguments, "refspecs");
    std::vector<char*> raw_refspecs;
    git_strarray refspecs = to_strarray(refspec_values, raw_refspecs);
    check_git(git_remote_fetch(remote.get(), refspec_values.empty() ? nullptr : &refspecs, &options, nullptr), "fetch remote");
    return json{{"remote", git_remote_name(remote.get()) != nullptr ? git_remote_name(remote.get()) : ""}};
}

[[nodiscard]] json call_git_push(const FileToolsContext& context, const json& arguments) {
    auto repository = open_repository(context, arguments);
    auto remote = lookup_remote(repository.get(), arguments);
    CredentialPayload payload = credential_payload(context, arguments);
    std::vector<std::string> refspec_values = string_array(arguments, "refspecs");
    if (refspec_values.empty()) {
        throw ToolError("push requires refspecs");
    }
    for (const std::string& refspec : refspec_values) {
        if (!refspec.empty() && refspec.front() == '+' && !optional_bool(arguments, "force", false)) {
            throw ToolError("force push refspecs require force=true");
        }
    }

    git_push_options options = GIT_PUSH_OPTIONS_INIT;
    apply_remote_callbacks(options.callbacks, payload);
    std::vector<char*> raw_refspecs;
    git_strarray refspecs = to_strarray(refspec_values, raw_refspecs);
    check_git(git_remote_push(remote.get(), &refspecs, &options), "push remote");
    return json{{"remote", git_remote_name(remote.get()) != nullptr ? git_remote_name(remote.get()) : ""}, {"refspecs", refspec_values}};
}

[[nodiscard]] json call_git_pull(const FileToolsContext& context, const json& arguments) {
    call_git_fetch(context, arguments);
    auto repository = open_repository(context, arguments);

    git_reference* raw_head = nullptr;
    check_git(git_repository_head(&raw_head, repository.get()), "read HEAD");
    git_ptr<git_reference, git_reference_free> head(raw_head, git_reference_free);

    git_reference* raw_upstream = nullptr;
    check_git(git_branch_upstream(&raw_upstream, head.get()), "read upstream branch");
    git_ptr<git_reference, git_reference_free> upstream(raw_upstream, git_reference_free);

    git_annotated_commit* raw_commit = nullptr;
    check_git(git_annotated_commit_from_ref(&raw_commit, repository.get(), upstream.get()), "prepare upstream commit");
    git_ptr<git_annotated_commit, git_annotated_commit_free> commit(raw_commit, git_annotated_commit_free);
    const git_annotated_commit* commits[] = {commit.get()};

    git_merge_analysis_t analysis{};
    git_merge_preference_t preference{};
    check_git(git_merge_analysis(&analysis, &preference, repository.get(), commits, 1), "analyze pull merge");
    if ((analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE) != 0) {
        return json{{"updated", false}, {"strategy", "up_to_date"}};
    }

    if ((analysis & GIT_MERGE_ANALYSIS_FASTFORWARD) != 0) {
        const git_oid* target_oid = git_annotated_commit_id(commit.get());
        git_reference* raw_updated = nullptr;
        check_git(git_reference_set_target(&raw_updated, head.get(), target_oid, "Fast-forward"), "fast-forward branch");
        git_ptr<git_reference, git_reference_free> updated(raw_updated, git_reference_free);

        git_checkout_options checkout_options = GIT_CHECKOUT_OPTIONS_INIT;
        checkout_options.checkout_strategy = optional_bool(arguments, "force", false) ? GIT_CHECKOUT_FORCE : GIT_CHECKOUT_SAFE;
        git_object* raw_target = nullptr;
        check_git(git_object_lookup(&raw_target, repository.get(), target_oid, GIT_OBJECT_COMMIT), "lookup fast-forward commit");
        git_ptr<git_object, git_object_free> target(raw_target, git_object_free);
        check_git(git_checkout_tree(repository.get(), target.get(), &checkout_options), "checkout fast-forward");
        return json{{"updated", true}, {"strategy", "fast_forward"}, {"oid", oid_string(target_oid)}};
    }

    git_merge_options merge_options = GIT_MERGE_OPTIONS_INIT;
    git_checkout_options checkout_options = GIT_CHECKOUT_OPTIONS_INIT;
    checkout_options.checkout_strategy = optional_bool(arguments, "force", false) ? GIT_CHECKOUT_FORCE : GIT_CHECKOUT_SAFE;
    check_git(git_merge(repository.get(), commits, 1, &merge_options, &checkout_options), "merge upstream");
    return json{{"updated", true}, {"strategy", "merge"}};
}

[[nodiscard]] std::string worktree_default_name(const fs::path& path) {
    const fs::path filename = path.filename();
    if (!filename.empty()) {
        return filename.string();
    }

    const fs::path parent_filename = path.parent_path().filename();
    if (!parent_filename.empty()) {
        return parent_filename.string();
    }

    throw ToolError("worktree_path must include a directory name");
}

[[nodiscard]] git_ptr<git_reference, git_reference_free> branch_ref_for_worktree(
    git_repository* repository,
    const json& arguments,
    const std::string& worktree_name
) {
    const bool has_branch = arguments.contains("branch");
    const bool has_ref = arguments.contains("ref");
    if (!has_branch && !has_ref) {
        return {nullptr, git_reference_free};
    }

    const std::string branch_name = has_branch ? require_string(arguments, "branch") : worktree_name;
    git_reference* raw_branch = nullptr;
    const int branch_result = git_branch_lookup(&raw_branch, repository, branch_name.c_str(), GIT_BRANCH_LOCAL);
    if (branch_result == 0) {
        return {raw_branch, git_reference_free};
    }
    if (branch_result != GIT_ENOTFOUND) {
        check_git(branch_result, "lookup worktree branch");
    }

    const std::string start_point = optional_string(arguments, "ref", "HEAD");
    git_object* raw_target = nullptr;
    check_git(git_revparse_single(&raw_target, repository, start_point.c_str()), "lookup worktree start point");
    git_ptr<git_object, git_object_free> target(raw_target, git_object_free);
    if (git_object_type(target.get()) != GIT_OBJECT_COMMIT) {
        throw ToolError("worktree start point must resolve to a commit");
    }

    check_git(git_branch_create(
        &raw_branch,
        repository,
        branch_name.c_str(),
        reinterpret_cast<git_commit*>(target.get()),
        0
    ), "create worktree branch");
    return {raw_branch, git_reference_free};
}

[[nodiscard]] json call_git_worktree_add(const FileToolsContext& context, const json& arguments) {
    std::scoped_lock lock(worktree_add_mutex());

    auto repository = open_repository(context, arguments);
    const fs::path worktree_path = context.resolve_path(require_string(arguments, "worktree_path"), false);
    const std::string name = optional_string(arguments, "name", worktree_default_name(worktree_path));
    auto branch_ref = branch_ref_for_worktree(repository.get(), arguments, name);

    git_worktree_add_options options = GIT_WORKTREE_ADD_OPTIONS_INIT;
    options.lock = optional_bool(arguments, "lock", false) ? 1 : 0;
    options.checkout_existing = optional_bool(arguments, "checkout_existing", false) ? 1 : 0;
    options.ref = branch_ref.get();

    git_worktree* raw = nullptr;
    check_git(
        git_worktree_add(&raw, repository.get(), name.c_str(), worktree_path.string().c_str(), &options),
        "add worktree"
    );
    git_ptr<git_worktree, git_worktree_free> worktree(raw, git_worktree_free);

    return json{
        {"name", git_worktree_name(worktree.get()) != nullptr ? git_worktree_name(worktree.get()) : name},
        {"path", relative_string(context.root_path(), worktree_path)},
        {"branch", branch_ref ? git_reference_shorthand(branch_ref.get()) : name},
        {"locked", optional_bool(arguments, "lock", false)},
    };
}

[[nodiscard]] json call_git_worktree_list(const FileToolsContext& context, const json& arguments) {
    auto repository = open_repository(context, arguments);
    git_strarray names{};
    check_git(git_worktree_list(&names, repository.get()), "list worktrees");

    json entries = json::array();
    for (std::size_t index = 0; index < names.count; ++index) {
        git_worktree* raw = nullptr;
        check_git(git_worktree_lookup(&raw, repository.get(), names.strings[index]), "lookup worktree");
        git_ptr<git_worktree, git_worktree_free> worktree(raw, git_worktree_free);
        const char* path = git_worktree_path(worktree.get());
        entries.push_back({
            {"name", git_worktree_name(worktree.get()) != nullptr ? git_worktree_name(worktree.get()) : names.strings[index]},
            {"path", path != nullptr ? relative_string(context.root_path(), path) : ""},
            {"valid", git_worktree_validate(worktree.get()) == 0},
        });
    }

    git_strarray_dispose(&names);
    return json{{"worktrees", std::move(entries)}};
}

void redact_object(json& object) {
    static constexpr std::array<std::string_view, 7U> keys = {
        "credentials",
        "password",
        "token",
        "ssh_passphrase",
        "ssh_private_key",
        "ssh_private_key_path",
        "secret",
    };

    if (!object.is_object()) {
        return;
    }

    for (auto it = object.begin(); it != object.end(); ++it) {
        if (std::find(keys.begin(), keys.end(), it.key()) != keys.end()) {
            it.value() = "[redacted]";
        } else if (it.value().is_object()) {
            redact_object(it.value());
        }
    }
}

} // namespace

json make_git_spec() {
    return make_tool_spec("git", "Run git repository operations using embedded libgit2 instead of a host git executable.", git_schema());
}

json redact_git_arguments(json arguments) {
    redact_object(arguments);
    return arguments;
}

json call_git(const FileToolsContext& context, const json& arguments) {
    configure_libgit2_sandbox_paths(context);

    const std::string op = require_string(arguments, "op");

    if (op == "init") return call_git_init(context, arguments);
    if (op == "status") return call_git_status(context, arguments);
    if (op == "diff") return call_git_diff(context, arguments);
    if (op == "add") return call_git_add(context, arguments);
    if (op == "commit") return call_git_commit(context, arguments);
    if (op == "head") return call_git_head(context, arguments);
    if (op == "log") return call_git_log(context, arguments);
    if (op == "branches") return call_git_branches(context, arguments);
    if (op == "remotes") return call_git_remotes(context, arguments);
    if (op == "checkout") return call_git_checkout(context, arguments);
    if (op == "branch_create") return call_git_branch_create(context, arguments);
    if (op == "branch_delete") return call_git_branch_delete(context, arguments);
    if (op == "reset") return call_git_reset(context, arguments);
    if (op == "merge") return call_git_merge(context, arguments);
    if (op == "clone") return call_git_clone(context, arguments);
    if (op == "fetch") return call_git_fetch(context, arguments);
    if (op == "pull") return call_git_pull(context, arguments);
    if (op == "push") return call_git_push(context, arguments);
    if (op == "worktree_add") return call_git_worktree_add(context, arguments);
    if (op == "worktree_list") return call_git_worktree_list(context, arguments);

    throw ToolError("unsupported git operation: " + op);
}

} // namespace criper
