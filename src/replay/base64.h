#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace bmv::base64 {

// URL-Safe Base64 decode (RFC 4648 §5).
// '-' = 62, '_' = 63. Tolerates missing '=' padding.
inline std::vector<uint8_t> decode(const std::string& input) {
    static const int8_t T[128] = {
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1, // 0-15
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1, // 16-31
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,62,-1,-1, // 32-47: '-'=62 at 45
        52,53,54,55,56,57,58,59, 60,61,-1,-1,-1,-1,-1,-1, // 48-63: '0'-'9'=52-61
        -1, 0, 1, 2, 3, 4, 5, 6,  7, 8, 9,10,11,12,13,14, // 64-79: 'A'-'O'=0-14
        15,16,17,18,19,20,21,22, 23,24,25,-1,-1,-1,-1,63, // 80-95: 'P'-'Z'=15-25, '_'=63 at 95
        -1,26,27,28,29,30,31,32, 33,34,35,36,37,38,39,40, // 96-111: 'a'-'o'=26-40
        41,42,43,44,45,46,47,48, 49,50,51,-1,-1,-1,-1,-1, // 112-127: 'p'-'z'=41-51
    };

    std::vector<uint8_t> output;
    output.reserve(input.size() * 3 / 4);

    int acc = 0;
    int bits = 0;

    for (char c : input) {
        if (c == '=') break;
        int idx = static_cast<unsigned char>(c);
        if (idx > 127) continue;
        int v = T[idx];
        if (v < 0) continue;

        acc = (acc << 6) | v;
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }

    return output;
}

} // namespace bmv::base64
