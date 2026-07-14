# criper-mcp

`criper-mcp` is a small MCP (Model Context Protocol) server focused on filesystem work. It exposes a compact set of file tools over HTTP using JSON-RPC, with optional Linux Landlock sandboxing to keep the server scoped to a chosen root directory.

## Name

The name `criper` is a joke. It comes from `creeper`, the Minecraft mob, but written the way the word is commonly pronounced in Portuguese.

## Why This Exists

This project is built to be:

- Efficient
- Resource-conscious
- Easy for a model to call correctly
- Simple to audit and maintain

Instead of depending on a large runtime stack, `criper-mcp` stays intentionally small:

- One native binary
- Flat JSON tool contracts
- A narrow filesystem-focused feature set
- Sandboxed execution rooted in a configured directory

That makes it a good fit for constrained environments or for situations where you want predictable behavior and low overhead instead of a heavyweight service.

## Third-Party Libraries

The server relies on two excellent single-header libraries:

- [`cpp-httplib`](https://github.com/yhirose/cpp-httplib) by `yhirose` for the HTTP server
- [`json.hpp` / nlohmann::json`](https://github.com/nlohmann/json) by `nlohmann` for JSON parsing and serialization
- [`libgit2`](https://libgit2.org/) for embedded git operations without requiring a host `git` executable

These dependencies are vendored locally under `third_party/`.

## Installation

### Requirements

- C++20 compiler
- CMake 3.31 or newer
- Linux if you want Landlock sandboxing

### Install Build Dependencies

Debian/Ubuntu:

```bash
sudo apt install build-essential cmake liblandlock-dev
```

Fedora/RHEL:

```bash
sudo dnf install gcc-c++ cmake liblandlock-devel
```

### Build

From the repository root:

```bash
cmake -S . -B build
cmake --build build
```

This produces the server binary at:

```text
./build/criper-mcp
```

If you prefer, you can copy that binary somewhere on your `PATH` manually.

## Running

You can configure the server either with CLI flags or environment variables.

### CLI

```bash
./build/criper-mcp \
  --root /path/to/workspace \
  --host 127.0.0.1 \
  --port 9999 \
  --sandbox-mode strict
```

### Environment Variables

```bash
CRIPER_MCP_ROOT=/path/to/workspace \
CRIPER_MCP_HOST=127.0.0.1 \
CRIPER_MCP_PORT=9999 \
CRIPER_MCP_SANDBOX_MODE=host-tools \
./build/criper-mcp
```

### Options

- `--root` / `CRIPER_MCP_ROOT`
  Root directory exposed by the server.
- `--host` / `CRIPER_MCP_HOST`
  Bind address. Default: `127.0.0.1`
- `--port` / `CRIPER_MCP_PORT`
  TCP port. Default: `9999`
- `--sandbox-mode` / `CRIPER_MCP_SANDBOX_MODE`
  Sandbox mode: `strict` or `host-tools`
- `--debug` / `CRIPER_MCP_DEBUG`
  Enable verbose debug logging

## How It Works

At a high level, the server is split into two parts:

1. HTTP + JSON-RPC handling
   This lives in [src/mcp_server.cpp](/home/cmartin/Projects/criper/src/mcp_server.cpp) and is responsible for request parsing, protocol responses, and endpoint handling.
2. Filesystem tool implementations
   These live under [src/fs_tools](/home/cmartin/Projects/criper/src/fs_tools) with one `.cpp` per `fs_*` tool, plus shared support code in [src/fs_tools/support.cpp](/home/cmartin/Projects/criper/src/fs_tools/support.cpp).

That split keeps the protocol layer decoupled from the tool layer:

- `mcp_server.cpp` handles transport and JSON-RPC
- `FileTools` dispatches MCP tool calls
- each `fs_*` tool owns its own schema and behavior
- shared parsing, path resolution, and process helpers live in the `fs_tools` support layer

## Sandboxing

The server uses Landlock on Linux to restrict filesystem access.

Two sandbox modes are available:

- `strict`
  Only the configured MCP root is visible to the server and its child processes.
- `host-tools`
  The configured root stays writable, and a small read-only runtime surface is exposed so host binaries such as `/bin/sed` can run. This mode is still constrained, but it is less isolated than `strict`.

`host-tools` currently allows read/execute access to the host runtime paths needed for common binaries and shared libraries, while still blocking writes outside the configured root.

If Landlock is not available, the server still runs, but without filesystem sandbox protection.

## API Endpoints

- `GET /`
  Health check
- `GET /health`
  Health check alias
- `POST /mcp`
  Main MCP JSON-RPC endpoint

## Tools

The server currently exposes these MCP tools:

- `fs_list`
  List files and directories
- `fs_find`
  Find files and directories by name or relative path
- `fs_read`
  Read a file
- `fs_tail`
  Read the last lines of a text file
- `fs_write`
  Write a file
- `fs_patch`
  Apply small text patches
- `fs_grep`
  Search file contents
- `fs_exec`
  Execute a command inside the configured root
- `fs_stat`
  Inspect file metadata
- `fs_chmod`
  Change permissions
- `fs_mkdir`
  Create directories
- `fs_copy`
  Copy files or directories
- `fs_move`
  Move or rename files or directories
- `fs_remove`
  Remove files or directories
- `git`
  Run git repository operations through embedded libgit2

### Git Tool Credentials

The `git` tool accepts one operation through the `op` field. Remote operations
(`clone`, `fetch`, `pull`, and `push`) can receive per-call credentials:

```json
{
  "op": "clone",
  "url": "git@github.com:owner/repo.git",
  "path": "repo",
  "credentials": {
    "username": "git",
    "ssh_private_key_path": "keys/id_ed25519",
    "ssh_public_key_path": "keys/id_ed25519.pub",
    "ssh_passphrase": "optional-passphrase"
  }
}
```

For SSH agent auth, use `"use_ssh_agent": true`. For HTTPS, use
`"username"` plus `"password"` or `"token"`. Credential fields are never
persisted and are redacted from debug logs.

For local repository fan-out, use `worktree_add` instead of cloning. `path`
points at the existing source repository and `worktree_path` points at the new
linked checkout:

```json
{
  "op": "worktree_add",
  "path": "dwcore",
  "worktree_path": "workspaces/dwcore-task",
  "branch": "dwcore-task"
}
```

Use `worktree_list` with `path` set to the source repository to inspect linked
worktrees.

## Repository Layout

- [src/main.cpp](/home/cmartin/Projects/criper/src/main.cpp)
  CLI entrypoint and configuration parsing
- [src/mcp_server.cpp](/home/cmartin/Projects/criper/src/mcp_server.cpp)
  HTTP server and JSON-RPC protocol handling
- [src/fs_tools](/home/cmartin/Projects/criper/src/fs_tools)
  One implementation file per filesystem MCP tool
- [src/sandbox.cpp](/home/cmartin/Projects/criper/src/sandbox.cpp)
  Landlock sandbox policy
- [src/sandbox_setup.cpp](/home/cmartin/Projects/criper/src/sandbox_setup.cpp)
  Sandbox initialization glue
- [include/criper](/home/cmartin/Projects/criper/include/criper)
  Shared project headers

## License

MIT
