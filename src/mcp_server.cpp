#include "mcp_server.hpp"

#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "criper/fs_tools.hpp"
#include "criper/fs_tools_support.hpp"
#include "httplib.h"
#include "json.hpp"

namespace criper {
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

class JsonRpcError final : public std::runtime_error {
public:
    JsonRpcError(const int code, std::string message)
        : std::runtime_error(std::move(message))
        , code_(code) {
    }

    [[nodiscard]] int code() const noexcept {
        return code_;
    }

private:
    int code_;
};

[[nodiscard]] const json& require_jsonrpc_object(const json& value, const std::string_view label) {
    if (!value.is_object()) {
        throw JsonRpcError(-32602, std::string(label) + " must be an object");
    }
    return value;
}

[[nodiscard]] std::string require_jsonrpc_string(const json& object, const std::string_view key) {
    if (!object.contains(key) || !object.at(key).is_string()) {
        throw JsonRpcError(-32602, "missing string argument: " + std::string(key));
    }
    return object.at(key).get<std::string>();
}

[[nodiscard]] std::string header_value_or_empty(const httplib::Request& request, const char* key) {
    const auto value = request.get_header_value(key);
    return value.empty() ? std::string() : value;
}

[[nodiscard]] std::string format_headers_for_log(const httplib::Headers& headers) {
    std::ostringstream stream;
    bool first = true;

    for (const auto& [key, value] : headers) {
        if (!first) {
            stream << ", ";
        }
        first = false;
        stream << key << '=' << value;
    }

    return stream.str();
}

void apply_cors_headers(httplib::Response& response, const httplib::Request& request) {
    const std::string origin = header_value_or_empty(request, "Origin");
    const std::string requested_headers = header_value_or_empty(request, "Access-Control-Request-Headers");
    const std::string requested_method = header_value_or_empty(request, "Access-Control-Request-Method");

    response.set_header("Access-Control-Allow-Origin", origin.empty() ? "*" : origin);
    response.set_header("Vary", "Origin, Access-Control-Request-Headers, Access-Control-Request-Method");
    response.set_header("Access-Control-Allow-Methods", requested_method.empty() ? "GET, POST, HEAD, OPTIONS" : requested_method);
    response.set_header(
        "Access-Control-Allow-Headers",
        requested_headers.empty()
            ? "Content-Type, Accept, Authorization, MCP-Session-Id, mcp-session-id, Last-Event-ID"
            : requested_headers
    );
    response.set_header("Access-Control-Max-Age", "86400");
}

[[nodiscard]] json make_jsonrpc_error(const json& id, const int code, std::string message) {
    return json{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {
            {"code", code},
            {"message", std::move(message)},
        }},
    };
}

[[nodiscard]] std::optional<json> dispatch_request(const fs::path& root_path, const bool debug_enabled, const json& request) {
    const json response_id = request.contains("id") ? request.at("id") : json(nullptr);

    if (!request.contains("jsonrpc") || !request.at("jsonrpc").is_string() || request.at("jsonrpc") != "2.0") {
        throw JsonRpcError(-32600, "jsonrpc must be \"2.0\"");
    }
    if (!request.contains("method") || !request.at("method").is_string()) {
        throw JsonRpcError(-32600, "method must be a string");
    }

    const std::string method = request.at("method").get<std::string>();
    const json params = request.contains("params") ? require_jsonrpc_object(request.at("params"), "params") : json::object();

    debug_log(
        debug_enabled,
        "jsonrpc method=" + method
            + " id=" + truncate_for_log(response_id.dump())
            + " params=" + truncate_for_log(params.dump())
    );

    if (method == "notifications/initialized") {
        debug_log(debug_enabled, "jsonrpc notification initialized");
        return std::nullopt;
    }

    FileTools tools(root_path, debug_enabled);
    if (method == "initialize") {
        auto response = json{
            {"jsonrpc", "2.0"},
            {"id", response_id},
            {"result", {
                {"protocolVersion", "2025-03-26"},
                {"capabilities", {
                    {"tools", {
                        {"listChanged", false},
                    }},
                }},
                {"serverInfo", {
                    {"name", "criper-mcp"},
                    {"version", "0.1.0"},
                }},
            }},
        };
        debug_log(debug_enabled, "jsonrpc response method=initialize body=" + truncate_for_log(response.dump()));
        return response;
    }

    if (method == "ping") {
        auto response = json{
            {"jsonrpc", "2.0"},
            {"id", response_id},
            {"result", json::object()},
        };
        debug_log(debug_enabled, "jsonrpc response method=ping body=" + truncate_for_log(response.dump()));
        return response;
    }

    if (method == "tools/list") {
        auto response = json{
            {"jsonrpc", "2.0"},
            {"id", response_id},
            {"result", {
                {"tools", tools.list_tools()},
            }},
        };
        debug_log(debug_enabled, "jsonrpc response method=tools/list body=" + truncate_for_log(response.dump()));
        return response;
    }

    if (method == "tools/call") {
        const std::string tool_name = require_jsonrpc_string(params, "name");
        const json tool_arguments = params.contains("arguments")
            ? require_jsonrpc_object(params.at("arguments"), "arguments")
            : json::object();

        auto response = json{
            {"jsonrpc", "2.0"},
            {"id", response_id},
            {"result", tools.call(tool_name, tool_arguments)},
        };
        debug_log(
            debug_enabled,
            "jsonrpc response method=tools/call tool=" + tool_name + " body=" + truncate_for_log(response.dump())
        );
        return response;
    }

    throw JsonRpcError(-32601, "method not found: " + method);
}

[[nodiscard]] std::optional<std::string> handle_jsonrpc_payload(
    const fs::path& root_path,
    const bool debug_enabled,
    const json& payload
) {
    if (payload.is_object()) {
        const auto response = dispatch_request(root_path, debug_enabled, payload);
        if (!response.has_value()) {
            return std::string();
        }
        return response->dump();
    }

    if (!payload.is_array()) {
        return make_jsonrpc_error(nullptr, -32600, "request body must be a JSON object or array").dump();
    }

    const json::array_t& requests = payload.get_ref<const json::array_t&>();
    if (requests.empty()) {
        return make_jsonrpc_error(nullptr, -32600, "batch requests must not be empty").dump();
    }

    json responses = json::array();
    for (const json& request : requests) {
        if (!request.is_object()) {
            responses.push_back(make_jsonrpc_error(nullptr, -32600, "batch entry must be a JSON object"));
            continue;
        }

        try {
            const auto response = dispatch_request(root_path, debug_enabled, request);
            if (response.has_value()) {
                responses.push_back(*response);
            }
        } catch (const JsonRpcError& error) {
            responses.push_back(make_jsonrpc_error(nullptr, error.code(), error.what()));
        }
    }

    if (responses.empty()) {
        return std::string();
    }

    return responses.dump();
}

} // namespace

McpServer::McpServer(
    std::filesystem::path root_path,
    std::string bind_address,
    const std::uint16_t port,
    const bool debug_enabled
)
    : root_path_(std::move(root_path))
    , bind_address_(std::move(bind_address))
    , port_(port)
    , debug_enabled_(debug_enabled) {
    std::filesystem::create_directories(root_path_);
    root_path_ = std::filesystem::weakly_canonical(root_path_);
}

const std::filesystem::path& McpServer::root_path() const noexcept {
    return root_path_;
}

const std::string& McpServer::bind_address() const noexcept {
    return bind_address_;
}

std::uint16_t McpServer::port() const noexcept {
    return port_;
}

bool McpServer::debug_enabled() const noexcept {
    return debug_enabled_;
}

std::optional<std::string> McpServer::handle_http_request(
    const std::string_view method,
    const std::string_view target,
    const std::string_view body
) const {
    debug_log(
        debug_enabled_,
        "http request method=" + std::string(method)
            + " target=" + std::string(target)
            + " body=" + truncate_for_log(body)
    );

    if (method == "GET" && (target == "/" || target == "/health")) {
        auto response = json{
            {"status", "ok"},
            {"root", root_path_.string()},
            {"mcpEndpoint", "/mcp"},
        }.dump();
        debug_log(debug_enabled_, "http response method=GET target=" + std::string(target) + " body=" + truncate_for_log(response));
        return response;
    }

    if (method == "HEAD" && (target == "/" || target == "/health" || target == "/mcp")) {
        return std::string();
    }

    const bool is_mcp_target = target == "/" || target == "/mcp";
    if (method != "POST" || !is_mcp_target) {
        debug_log(debug_enabled_, "http request rejected method=" + std::string(method) + " target=" + std::string(target));
        return std::nullopt;
    }

    try {
        const json parsed = json::parse(body);
        const auto serialized = handle_jsonrpc_payload(root_path_, debug_enabled_, parsed);
        if (!serialized.has_value()) {
            debug_log(debug_enabled_, "jsonrpc request completed without HTTP response");
            return std::nullopt;
        }
        if (serialized->empty()) {
            debug_log(debug_enabled_, "jsonrpc notification completed without response");
            return std::string();
        }
        debug_log(debug_enabled_, "http response method=POST target=/mcp body=" + truncate_for_log(*serialized));
        return serialized;
    } catch (const json::parse_error& error) {
        const auto response = make_jsonrpc_error(nullptr, -32700, error.what()).dump();
        debug_log(debug_enabled_, "json parse_error message=" + std::string(error.what()));
        return response;
    } catch (const JsonRpcError& error) {
        const auto response = make_jsonrpc_error(nullptr, error.code(), error.what()).dump();
        debug_log(debug_enabled_, "jsonrpc error code=" + std::to_string(error.code()) + " message=" + error.what());
        return response;
    }
}

void McpServer::run() const {
    httplib::Server server;

    server.Get("/", [this](const httplib::Request& request, httplib::Response& response) {
        apply_cors_headers(response, request);
        response.set_content(handle_http_request("GET", "/", "").value_or("{}"), "application/json");
    });

    server.Get("/health", [this](const httplib::Request& request, httplib::Response& response) {
        apply_cors_headers(response, request);
        response.set_content(handle_http_request("GET", "/health", "").value_or("{}"), "application/json");
    });

    server.Options(R"(.*)", [this](const httplib::Request& request, httplib::Response& response) {
        apply_cors_headers(response, request);
        response.status = 204;
        debug_log(
            debug_enabled_,
            "http status=204 method=OPTIONS path=" + request.path
                + " headers={" + format_headers_for_log(request.headers) + "}"
        );
    });

    server.Post("/mcp", [this](const httplib::Request& request, httplib::Response& response) {
        apply_cors_headers(response, request);
        const auto body = handle_http_request("POST", request.path, request.body);
        if (!body.has_value()) {
            response.status = 404;
            response.set_content(R"({"error":"not found"})", "application/json");
            debug_log(debug_enabled_, "http status=404 path=" + request.path);
            return;
        }
        if (body->empty()) {
            response.status = 204;
            debug_log(debug_enabled_, "http status=204 path=" + request.path);
            return;
        }
        response.set_content(*body, "application/json");
        debug_log(debug_enabled_, "http status=" + std::to_string(response.status) + " path=" + request.path);
    });

    server.Post("/", [this](const httplib::Request& request, httplib::Response& response) {
        apply_cors_headers(response, request);
        const auto body = handle_http_request("POST", request.path, request.body);
        if (!body.has_value()) {
            response.status = 404;
            response.set_content(R"({"error":"not found"})", "application/json");
            debug_log(debug_enabled_, "http status=404 path=" + request.path);
            return;
        }
        if (body->empty()) {
            response.status = 204;
            debug_log(debug_enabled_, "http status=204 path=" + request.path);
            return;
        }
        response.set_content(*body, "application/json");
        debug_log(debug_enabled_, "http status=" + std::to_string(response.status) + " path=" + request.path);
    });

    server.set_error_handler([this](const httplib::Request& request, httplib::Response& response) {
        apply_cors_headers(response, request);
        if (response.status == 404) {
            response.set_content(R"({"error":"not found"})", "application/json");
        }
        debug_log(
            debug_enabled_,
            "httplib error_handler method=" + std::string(request.method) + " status="
                + std::to_string(response.status) + " path=" + request.path
                + " headers={" + format_headers_for_log(request.headers) + "}"
        );
    });

    if (!server.listen(bind_address_.c_str(), port_)) {
        throw std::runtime_error("failed to listen on requested address");
    }
}

} // namespace criper
