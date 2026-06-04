#include "replay_factory.h"
#include "brd_parser.h"
#include "lr2rep_parser.h"
#include <cstdio>
#include <cctype>

namespace bmv {

std::optional<ReplayInput> parse_replay(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) {
#ifdef BMV_DEBUG
        std::fprintf(stderr, "[parse_replay] no extension: %s\n", path.c_str());
#endif
        return std::nullopt;
    }

    std::string ext = path.substr(dot);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (ext == ".brd") {
        BrdParser parser;
        auto result = parser.parse_replay_input(path);
        if (result.events.empty()) return std::nullopt;
        return result;
    } else if (ext == ".lr2rep") {
        Lr2RepParser parser;
        auto result = parser.parse_replay_input(path);
        if (result.events.empty()) return std::nullopt;
        return result;
    }

#ifdef BMV_DEBUG
    std::fprintf(stderr, "[parse_replay] unsupported format: %s\n", ext.c_str());
#endif
    return std::nullopt;
}

} // namespace bmv
