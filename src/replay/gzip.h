// Native GZIP decompression — zero external processes, zero temp files.
// Manually parses GZIP header (FLG-aware), reads ISIZE from footer,
// delegates raw DEFLATE to miniz tinfl_decompress_mem_to_mem().
#pragma once
#include <vector>
#include <cstdint>
#include <cstdio>
#include "miniz.h"

namespace bmv::gzip {

inline std::vector<uint8_t> decompress(const void* data, size_t size) {
    auto* p = static_cast<const uint8_t*>(data);

    if (size < 18 || p[0] != 0x1F || p[1] != 0x8B || p[2] != 8) {
        std::fprintf(stderr, "[gzip] not a valid GZIP stream\n");
        return {};
    }

    uint8_t flg = p[3];
    size_t pos = 10; // skip ID1 ID2 CM FLG MTIME[4] XFL OS

    // FEXTRA — bit 2
    if (flg & 0x04) {
        if (pos + 2 > size) return {};
        uint16_t xlen = static_cast<uint16_t>(p[pos]) |
                        (static_cast<uint16_t>(p[pos + 1]) << 8);
        pos += 2 + xlen;
        if (pos > size) return {};
    }

    // FNAME — bit 3 (null-terminated)
    if (flg & 0x08) {
        while (pos < size && p[pos] != 0) ++pos;
        if (pos < size) ++pos;
    }

    // FCOMMENT — bit 4 (null-terminated)
    if (flg & 0x10) {
        while (pos < size && p[pos] != 0) ++pos;
        if (pos < size) ++pos;
    }

    // FHCRC — bit 1 (2-byte header CRC16)
    if (flg & 0x02) {
        pos += 2;
    }

    // Footer is last 8 bytes: CRC32 (4) + ISIZE (4)
    if (pos + 8 > size) return {};

    const uint8_t* deflate_start = p + pos;
    size_t deflate_size = (p + size - 8) - deflate_start;

    if (deflate_size == 0) return {};

    const uint8_t* footer = p + size - 8;
    uint32_t isize = static_cast<uint32_t>(footer[4]) |
                     (static_cast<uint32_t>(footer[5]) << 8) |
                     (static_cast<uint32_t>(footer[6]) << 16) |
                     (static_cast<uint32_t>(footer[7]) << 24);

    size_t out_cap = isize ? isize : (deflate_size * 4 + 64);
    std::vector<uint8_t> out(out_cap);

    size_t written = tinfl_decompress_mem_to_mem(
        out.data(), out_cap,
        deflate_start, deflate_size,
        0 // raw DEFLATE, no zlib/gzip header
    );

    if (written == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
        out.resize(out_cap * 2);
        written = tinfl_decompress_mem_to_mem(
            out.data(), out_cap * 2,
            deflate_start, deflate_size,
            0
        );
        if (written == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
            std::fprintf(stderr, "[gzip] tinfl_decompress_mem_to_mem failed\n");
            return {};
        }
    }

    out.resize(written);
    return out;
}

} // namespace bmv::gzip
