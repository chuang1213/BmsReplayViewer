#pragma once

#include <array>
#include <cstdint>

namespace bmv {

/**
 * @brief 随机算法类型
 *
 * 不同回放格式使用不同的随机算法来生成 lane 排列：
 *   - OldBRD : 旧版 beatoraja BRD 使用 Java LCG (java.util.Random 兼容)
 *   - LR2    : LR2 使用自实现的 Mersenne Twister (1998 版，特殊种子初始化)
 */
enum class RandomType {
    OldBRD,
    LR2,
};

/**
 * @brief 计算 shuffle_pattern 的统一纯函数接口
 *
 * 不同格式的回放使用不同的随机算法，但最终输出统一格式的 shuffle_pattern[8]：
 *   shuffle_pattern[display_lane] = bms_lane
 *   其中 0 = scratch，1-7 = keys（白键/黑键顺序由 BMS 定义决定）
 *
 * 这是纯函数，无副作用，线程安全。
 *
 * @param seed  随机种子（正整数，由 replay 文件提供）
 * @param type  随机算法类型
 * @param param 算法特定参数：
 *              - RandomType::OldBRD : random_option (2 = keys only, 9 = keys + scratch)
 *              - RandomType::LR2    : L (按键数，通常 7 表示 7-key 模式)
 * @return std::array<int, 8> shuffle_pattern (统一 lane 格式)
 *
 * === 坐标转换原理 ===
 *
 * 内部各算法的输入/输出坐标系不统一，需要转换为 BMV 统一格式：
 *   - beatoraja display position 0-6 (keys)   → 统一 display lane 1-7
 *   - beatoraja display position 7   (scratch) → 统一 display lane 0
 *   - beatoraja BMS lane 0-6 (keys)            → 统一 BMS lane 1-7
 *   - beatoraja BMS lane 7   (scratch)         → 统一 BMS lane 0
 *
 * 转换公式：
 *   display_our = (display_bev == 7) ? 0 : display_bev + 1
 *   bms_our     = (bms_bev     == 7) ? 0 : bms_bev     + 1
 *
 * === 验证过的测试用例 ===
 *
 * OldBRD (random_option = 2, keys only)：
 *   seed=2871559 → {0, 6, 5, 3, 7, 1, 2, 4}
 *   seed=1432039 → {0, 5, 6, 7, 4, 2, 3, 1}
 *
 * LR2 (L = 7)：
 *   seed=24332 → {7, 1, 2, 0, 6, 3, 5, 4}
 *   (源数据：beatoraja display_to_bms = {0, 1, 7, 5, 2, 4, 3, 6}，经坐标转换得到)
 */
std::array<int, 8> compute_shuffle_pattern(int seed, RandomType type, int param);

} // namespace bmv
