#pragma once

#include "types.h"

#include <json.hpp>

#include <optional>

namespace bmv {

/**
 * @brief 解析新版 BRD（有 keyinput 字段，base64 + gzip 压缩的二进制数据）
 *
 * 新版 BRD 格式（beatoraja 较新版本）将按键记录压缩后存放在 JSON 的
 * `keyinput` 字段中（URL-safe base64 编码 + GZIP 压缩）。
 *
 * 解码后是连续的 9 字节帧序列：
 *   byte[0]     = 有符号 keycode（>0 表示按下，<0 表示松开）
 *   byte[1..8]  = int64_t 时间戳（LE，微秒）
 *
 * keycode 编码：0-6 = keys（lane 1-7），7 = scratch（lane 0）
 *
 * 元数据：
 *   - `laneShufflePattern[0][i]`：beatoraja 格式的 display→BMS lane 映射，
 *     本函数自动转换为统一格式（统一格式：0=scratch, 1-7=keys）。
 *   - 若不存在则保持恒等映射 `{0,1,2,3,4,5,6,7}`。
 *
 * @param j     已解析的 BRD 顶层 JSON
 * @param error 失败时填充结构化错误信息（可为 nullptr）
 * @return 成功返回 UnifiedReplay；失败返回 std::nullopt 并（若提供）填充 error
 */
std::optional<UnifiedReplay> parse_brd_new(const nlohmann::json& j,
                                            ParseError* error = nullptr);

/**
 * @brief 解析旧版 BRD（有 keylog 字段，JSON 数组）
 *
 * 旧版 BRD 格式（beatoraja 0.8.6 及更早版本）直接以 JSON 数组形式记录按键：
 *   [
 *     {"presstime": int64, "keycode": 0-7, "pressed": bool?},
 *     ...
 *   ]
 *
 * keycode 编码：0-6 = keys（lane 1-7），7 = scratch（lane 0）
 * `pressed` 字段可选：缺省或 false 视为松开事件。
 *
 * 元数据：
 *   - `randomoption` + `randomoptionseed`：当 random_option 为 2 或 9 时
 *     调用 `compute_shuffle_pattern(RandomType::OldBRD)` 还原 shuffle_pattern。
 *   - 若 random_option 非 2/9 或缺省，shuffle_pattern 保持恒等映射。
 *
 * @param j     已解析的 BRD 顶层 JSON
 * @param error 失败时填充结构化错误信息（可为 nullptr）
 * @return 成功返回 UnifiedReplay；失败返回 std::nullopt 并（若提供）填充 error
 */
std::optional<UnifiedReplay> parse_brd_old(const nlohmann::json& j,
                                            ParseError* error = nullptr);

} // namespace bmv
