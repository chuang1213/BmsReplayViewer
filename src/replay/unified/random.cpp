#include "random.h"
#include "../java_random.h"
#include "../lr2_random.h"

namespace bmv {

std::array<int, 8> compute_shuffle_pattern(int seed, RandomType type, int param) {
    std::array<int, 8> result = {0, 1, 2, 3, 4, 5, 6, 7};

    switch (type) {
    case RandomType::OldBRD: {
        // build_brd_random_pattern 内部已经完成坐标转换，直接输出统一格式
        int shuffle[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        build_brd_random_pattern(static_cast<int32_t>(param),
                                 static_cast<int64_t>(seed),
                                 shuffle);
        for (int i = 0; i < 8; ++i)
            result[i] = shuffle[i];
        break;
    }

    case RandomType::LR2: {
        // build_random_lane_pattern 输出 beatoraja 格式的 display_to_bms
        // (索引 0-6 = keys, 索引 7 = scratch)，需转换为统一格式
        int display_to_bms[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        int bms_to_display[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        build_random_lane_pattern(seed, param, display_to_bms, bms_to_display);

        for (int display_bev = 0; display_bev < 8; ++display_bev) {
            int display_our = (display_bev == 7) ? 0 : display_bev + 1;
            int bms_bev     = display_to_bms[display_bev];
            int bms_our     = (bms_bev == 7) ? 0 : bms_bev + 1;
            result[display_our] = bms_our;
        }
        break;
    }
    }

    return result;
}

} // namespace bmv
