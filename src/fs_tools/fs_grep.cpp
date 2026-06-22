#include "criper/fs_tool_registry.hpp"

#include <fstream>
#include <deque>
#include <cctype>
#include <cstdint>

namespace criper {

namespace {

// Replace invalid UTF-8 sequences with U+FFFD to ensure strings are valid for JSON.
static std::string sanitize_utf8(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    const unsigned char* s = reinterpret_cast<const unsigned char*>(in.data());
    size_t i = 0;
    const size_t n = in.size();
    auto append_replacement = [&out]() {
        // U+FFFD in UTF-8
        out.push_back(static_cast<char>(0xEF));
        out.push_back(static_cast<char>(0xBF));
        out.push_back(static_cast<char>(0xBD));
    };

    while (i < n) {
        unsigned char c = s[i];
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
            ++i;
            continue;
        }
        // Determine length
        size_t len = 0;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        else {
            append_replacement();
            ++i;
            continue;
        }

        if (i + len > n) {
            append_replacement();
            break;
        }

        bool ok = true;
        for (size_t j = 1; j < len; ++j) {
            if ((s[i + j] & 0xC0) != 0x80) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            append_replacement();
            ++i;
            continue;
        }

        for (size_t j = 0; j < len; ++j) out.push_back(static_cast<char>(s[i + j]));
        i += len;
    }

    return out;
}

// Heuristic: treat file as binary if it contains a NUL or many non-printable chars in a sample.
static bool is_binary_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    const size_t sample_size = 4096;
    std::string buf;
    buf.resize(sample_size);
    in.read(&buf[0], static_cast<std::streamsize>(sample_size));
    std::streamsize got = in.gcount();
    if (got <= 0) return false;
    size_t nul_count = 0;
    size_t non_print = 0;
    for (std::streamsize i = 0; i < got; ++i) {
        unsigned char c = static_cast<unsigned char>(buf[i]);
        if (c == 0) ++nul_count;
        // consider printable: tab, newline, carriage return, and 0x20-0x7E
        if (!(c == 9 || c == 10 || c == 13 || (c >= 0x20 && c <= 0x7E))) ++non_print;
    }
    if (nul_count > 0) return true;
    double ratio = static_cast<double>(non_print) / static_cast<double>(got);
    return ratio > 0.30; // heuristic
}


[[nodiscard]] json grep_schema() {
    json schema = base_schema();
    schema["properties"] = {
        {"pattern", {{"type", "string"}, {"description", "Text or regex to search for."}}},
        {"path", {{"type", "string"}, {"description", "File or directory to search. Default: ."}}},
        {"max_results", {{"type", "integer"}, {"minimum", 1}, {"description", "Maximum matches to return."}}},
        {"context", {{"type", "integer"}, {"minimum", 0}, {"description", "Include N lines before and after each match."}}},
        {"case_sensitive", {{"type", "boolean"}, {"description", "Case-sensitive search. Default: false"}}},
        {"regex", {{"type", "boolean"}, {"description", "Treat pattern as a regex. Default: false"}}},
    };
    schema["required"] = json::array({"pattern"});
    return schema;
}

} // namespace

json make_fs_grep_spec() {
    return make_tool_spec("fs_grep", "Search through files for matching text.", grep_schema());
}

json call_fs_grep(const FileToolsContext& context, const json& arguments) {
    const fs::path path = context.resolve_path(optional_string(arguments, "path", "."), true);
    const std::string pattern = require_string(arguments, "pattern");
    const std::size_t max_results = optional_size(arguments, "max_results", 100U);
    const std::size_t context_lines = optional_size(arguments, "context", 0U);
    const bool case_sensitive = optional_bool(arguments, "case_sensitive", false);
    const bool regex_mode = optional_bool(arguments, "regex", false);

    std::optional<std::regex> matcher;
    if (regex_mode) {
        const auto flags = case_sensitive
            ? std::regex::ECMAScript
            : static_cast<std::regex::flag_type>(std::regex::ECMAScript | std::regex::icase);
        matcher.emplace(pattern, flags);
    }

    auto line_matches = [&](const std::string& line) {
        return text_matches_pattern(line, pattern, case_sensitive, matcher ? &*matcher : nullptr);
    };

    auto search_file = [&](const fs::path& file_path, json& matches, std::size_t& count) {
        if (is_binary_file(file_path)) {
            return;
        }

        std::ifstream input(file_path);
        if (!input) return;

        std::deque<std::pair<std::uint64_t, std::string>> prev; // previous lines
        std::deque<std::pair<std::uint64_t, std::string>> lookahead; // next lines

        std::string line;
        std::uint64_t line_no = 0;

        // Read first line as current
        if (!std::getline(input, line)) return;
        ++line_no;
        std::pair<std::uint64_t, std::string> curr{line_no, line};

        // Pre-fill lookahead with up to context_lines lines
        for (std::size_t i = 0; i < context_lines; ++i) {
            if (!std::getline(input, line)) break;
            ++line_no;
            lookahead.emplace_back(line_no, line);
        }

        while (true) {
            if (count >= max_results) return;

            const std::string& curr_text = curr.second;
            if (line_matches(curr_text)) {
                json surrounding = json::array();
                for (const auto& p : prev) {
                    surrounding.push_back({
                        {"line", static_cast<std::uint64_t>(p.first)},
                        {"text", sanitize_utf8(p.second)},
                        {"match", false},
                    });
                }
                surrounding.push_back({
                    {"line", static_cast<std::uint64_t>(curr.first)},
                    {"text", sanitize_utf8(curr_text)},
                    {"match", true},
                });
                for (const auto& p : lookahead) {
                    surrounding.push_back({
                        {"line", static_cast<std::uint64_t>(p.first)},
                        {"text", sanitize_utf8(p.second)},
                        {"match", false},
                    });
                }

                matches.push_back({
                    {"path", relative_string(context.root_path(), file_path)},
                    {"line", static_cast<std::uint64_t>(curr.first)},
                    {"text", sanitize_utf8(curr_text)},
                    {"context", std::move(surrounding)},
                });
                ++count;
                if (count >= max_results) return;
            }

            // advance window
            prev.emplace_back(curr);
            if (prev.size() > context_lines) prev.pop_front();

            if (!lookahead.empty()) {
                curr = lookahead.front();
                lookahead.pop_front();
                if (std::getline(input, line)) {
                    ++line_no;
                    lookahead.emplace_back(line_no, line);
                }
                continue;
            }

            // lookahead empty -> try to read next line as current
            if (std::getline(input, line)) {
                ++line_no;
                curr = {line_no, line};
                // refill lookahead
                for (std::size_t i = 0; i < context_lines; ++i) {
                    if (!std::getline(input, line)) break;
                    ++line_no;
                    lookahead.emplace_back(line_no, line);
                }
                continue;
            }

            break;
        }
    };

    json matches = json::array();
    std::size_t count = 0U;
    if (fs::is_regular_file(path)) {
        search_file(path, matches, count);
    } else if (fs::is_directory(path)) {
        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (count >= max_results) {
                break;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
            search_file(entry.path(), matches, count);
        }
    } else {
        throw ToolError("path is not a regular file or directory");
    }

    return json{
        {"path", relative_string(context.root_path(), path)},
        {"pattern", pattern},
        {"regex", regex_mode},
        {"case_sensitive", case_sensitive},
        {"matches", std::move(matches)},
        {"truncated", count >= max_results},
    };
}

} // namespace criper
