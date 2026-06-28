#include "parser.h"
#include "brd_parser.h"
#include "lr2_parser.h"

#include "../gzip.h"
#include "../../util/fs_util.h"

#include <json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>
#include <vector>

namespace bmv {

namespace {

// Lowercase a string in-place.
std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Extract file extension (lowercased, includes leading '.'), or "" if none.
std::string get_extension(const std::string& path) {
    const auto pos = path.find_last_of(".\\/");
    if (pos == std::string::npos) return {};
    if (path[pos] != '.') return {};
    std::string ext = path.substr(pos);
    return to_lower(std::move(ext));
}

// Fill all four ParseError fields in one call.
void set_error(ParseError* error, const std::string& file_path,
               std::string stage, std::string reason, std::string context) {
    if (!error) return;
    error->file_path = file_path;
    error->stage     = std::move(stage);
    error->reason    = std::move(reason);
    error->context   = std::move(context);
}

// Read whole file as raw bytes (cross-platform path encoding via fs_util).
std::vector<uint8_t> read_file(const std::string& path) {
    return read_file_binary(path);
}

// Forward + tag the sub-parser's ParseError with the originating file_path.
using BrdSubParser = std::optional<UnifiedReplay> (*)(const nlohmann::json&,
                                                       ParseError*);

std::optional<UnifiedReplay> forward(
        BrdSubParser fn, const nlohmann::json& j, const std::string& file_path,
        const std::string& stage_tag, ParseError* error) {
    ParseError inner{};
    auto r = fn(j, &inner);
    if (!r.has_value()) {
        if (error) {
            error->file_path = file_path;
            error->stage     = stage_tag + ":" + inner.stage;
            error->reason    = inner.reason;
            error->context   = inner.context;
        }
        return std::nullopt;
    }
    return r;
}

} // namespace

// --- Dispatcher ---

std::optional<UnifiedReplay> parse_replay(const std::string& path,
                                           ParseError* error) {
    const std::string ext = get_extension(path);

    if (ext.empty()) {
        set_error(error, path, "extension", "no_extension",
                  "parse_replay: file has no extension: " + path);
        return std::nullopt;
    }

    if (ext == ".lr2rep") {
        auto raw = read_file(path);
        if (raw.empty()) {
            set_error(error, path, "read", "file_open_failed",
                      "parse_replay: cannot read file: " + path);
            return std::nullopt;
        }

        ParseError inner{};
        auto r = parse_lr2(raw, &inner);
        if (!r.has_value()) {
            if (error) {
                error->file_path = path;
                error->stage     = inner.stage.empty() ? "parse" : inner.stage;
                error->reason    = inner.reason;
                error->context   = inner.context;
            }
            return std::nullopt;
        }
        return r;
    }

    if (ext == ".brd") {
        auto raw = read_file(path);
        if (raw.empty()) {
            set_error(error, path, "read", "file_open_failed",
                      "parse_replay: cannot read file: " + path);
            return std::nullopt;
        }

        auto decompressed = gzip::decompress(raw.data(), raw.size());
        if (decompressed.empty()) {
            set_error(error, path, "gzip", "gzip_decompress_failed",
                      "parse_replay: gzip decompress failed for: " + path);
            return std::nullopt;
        }

        std::string text(decompressed.begin(), decompressed.end());
        auto j = nlohmann::json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            set_error(error, path, "json", "json_parse_failed",
                      "parse_replay: failed to parse BRD JSON for: " + path);
            return std::nullopt;
        }

        // Version detection: keyinput → new, keylog → old
        if (j.contains("keyinput") && j["keyinput"].is_string()) {
            return forward(&parse_brd_new, j, path, "brd_new", error);
        }
        if (j.contains("keylog") && j["keylog"].is_array()) {
            return forward(&parse_brd_old, j, path, "brd_old", error);
        }

        set_error(error, path, "version_detect", "unknown_brd_version",
                  "parse_replay: BRD JSON has neither 'keyinput' nor 'keylog': " +
                  path);
        return std::nullopt;
    }

    set_error(error, path, "extension", "unsupported_extension",
              "parse_replay: unsupported extension '" + ext +
              "' (only .brd and .lr2rep)");
    return std::nullopt;
}

} // namespace bmv
