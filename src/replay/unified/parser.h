#pragma once

#include "types.h"

#include <optional>
#include <string>

namespace bmv {

/**
 * @brief 回放文件解析工厂 — 根据文件扩展名分派到具体解析器
 *
 * 支持的扩展名（大小写不敏感）：
 *   - .brd     → Beatoraja 回放（GZIP 压缩的 JSON，根据 keyinput/keylog 字段
 *                自动分派到 parse_brd_new 或 parse_brd_old）
 *   - .lr2rep  → LR2 回放（12 字节 LE 记录流）
 *
 * 不支持的扩展名：返回 std::nullopt，error 字段填充
 *   error.stage   = "extension"
 *   error.reason  = "unsupported_extension"
 *   error.context = 实际的扩展名字符串
 *
 * 通用错误字段语义：
 *   - error.file_path：用户传入的路径
 *   - error.stage    ：出错的阶段 ("read" / "gzip" / "json" /
 *                                "version_detect" / "extension" /
 *                                "parse" / "dispatch")
 *   - error.reason   ：机器可读的简短代号
 *   - error.context  ：人类可读的详细说明
 *
 * @param path  回放文件路径
 * @param error 失败时填充结构化错误信息（可为 nullptr）
 * @return 成功返回 UnifiedReplay；失败返回 std::nullopt 并（若提供）填充 error
 */
std::optional<UnifiedReplay> parse_replay(const std::string& path,
                                           ParseError* error = nullptr);

} // namespace bmv
