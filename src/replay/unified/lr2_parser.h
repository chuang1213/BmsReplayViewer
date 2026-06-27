#pragma once

#include "types.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace bmv {

/**
 * @brief 解析 LR2 (.lr2rep) 回放文件原始字节流，输出 UnifiedReplay
 *
 * LR2REP 格式 (Lunatic Rave 2) 是固定 12 字节记录的二进制流：
 *   record = time_ms (i32 LE) | op (i32 LE) | value (i32 LE)
 *
 * 分类：
 *   - op < 40  : 按键事件
 *       op 0          = scratch → lane 0
 *       op 1-7        = keys   → lane 1-7
 *       op >= 8       = 忽略 (例 op 10 为另一方向转盘，LR2 不判定)
 *       value == 1    = press  (放入 press_events)
 *       value == 0    = release (放入 release_events)
 *       value 其他值  = 忽略
 *
 *   - op >= 40 : 元数据
 *       op 103 = P1 random_mode (0=Off, 1=Mirror, 2=Random, 3=SRandom, 4=RRandom)
 *       op 153 = P2 random_mode
 *       op 200 = seed (MT19937 seed)
 *       op 210 = 判定记录 (逐 note 的判定值，存入 metadata.judgements)
 *
 * 当 random_mode 为 Random(2) 或 RRandom(4) 时，调用
 *   compute_shuffle_pattern(seed, RandomType::LR2, L=7)
 * 计算 shuffle_pattern；其他模式 (Off/Mirror/SRandom) shuffle_pattern 保持恒等。
 *
 * @param raw   .lr2rep 文件的原始字节流 (工厂函数负责读文件)
 * @param error 失败时填充结构化错误信息 (可为 nullptr)
 * @return 成功返回 UnifiedReplay；失败返回 std::nullopt 并 (若提供) 填充 error
 */
std::optional<UnifiedReplay> parse_lr2(const std::vector<uint8_t>& raw,
                                        ParseError* error = nullptr);

} // namespace bmv
