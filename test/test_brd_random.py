#!/usr/bin/env python3
"""
验证 BRD 随机算法

测试用例1：seed=2871559, random_option=2
BRD 原始输出：[5, 4, 2, 6, 0, 1, 3, 7]
预期统一格式：[0, 6, 5, 3, 7, 1, 2, 4]

测试用例2：seed=1432039, random_option=2
BRD 原始输出：[4, 5, 6, 3, 1, 2, 0, 7]
预期统一格式：[0, 5, 6, 7, 4, 2, 3, 1]
"""

class JavaRandom:
    def __init__(self, seed):
        self.MULTIPLIER = 0x5DEECE66D
        self.ADDEND = 0xB
        self.MASK = 0xFFFFFFFFFFFF
        self.seed = (seed ^ self.MULTIPLIER) & self.MASK
    
    def next(self, bits):
        self.seed = (self.seed * self.MULTIPLIER + self.ADDEND) & self.MASK
        return (self.seed >> (48 - bits)) & ((1 << bits) - 1)
    
    def nextInt(self, n):
        if (n & -n) == n:  # n is power of 2
            return (self.next(31) * n) >> 31
        
        while True:
            bits = self.next(31) & 0x7FFFFFFF
            val = bits % n
            if bits - val + (n - 1) >= 0:
                return val


def generate_brd_pattern(random_option, seed):
    """生成 beatoraja 格式的 pattern"""
    rng = JavaRandom(seed)
    
    if random_option == 2:
        keys = [0, 1, 2, 3, 4, 5, 6]
    elif random_option == 9:
        keys = [0, 1, 2, 3, 4, 5, 6, 7]
    else:
        return list(range(8))
    
    result = [0, 1, 2, 3, 4, 5, 6, 7]
    available = keys.copy()
    
    for lane in keys:
        r = rng.nextInt(len(available))
        result[lane] = available[r]
        available.pop(r)
    
    return result


def convert_to_unified(beatoraja_pattern):
    """
    将 beatoraja 格式的 pattern 转换为统一格式
    
    beatoraja_pattern[i] = BMS lane at beatoraja display position i
    return: unified_pattern[i] = BMS lane at unified display lane i
    
    转换规则：
    - beatoraja display 0-6 → unified display 1-7
    - beatoraja display 7 → unified display 0
    - beatoraja BMS lane 0-6 → unified BMS lane 1-7
    - beatoraja BMS lane 7 → unified BMS lane 0
    """
    unified_pattern = [0] * 8
    for display_bev in range(8):
        bms_bev = beatoraja_pattern[display_bev]
        # Convert beatoraja display position to unified display lane
        display_our = 0 if display_bev == 7 else display_bev + 1
        # Convert beatoraja BMS lane to unified BMS lane
        bms_our = 0 if bms_bev == 7 else bms_bev + 1
        unified_pattern[display_our] = bms_our
    return unified_pattern


def test_case(seed, expected_brd, expected_unified):
    print(f"测试 seed={seed}, random_option=2")
    
    # 生成 beatoraja 格式
    brd_pattern = generate_brd_pattern(2, seed)
    print(f"  beatoraja 格式: {brd_pattern}")
    print(f"  预期 beatoraja: {expected_brd}")
    
    if brd_pattern != expected_brd:
        print(f"  结果: FAIL (beatoraja 格式不匹配)")
        return False
    
    # 转换为统一格式
    unified_pattern = convert_to_unified(brd_pattern)
    print(f"  统一格式: {unified_pattern}")
    print(f"  预期统一: {expected_unified}")
    
    if unified_pattern != expected_unified:
        print(f"  结果: FAIL (统一格式不匹配)")
        return False
    
    print(f"  结果: PASS")
    return True


def main():
    print("=" * 60)
    print("BRD 随机算法测试")
    print("=" * 60)
    
    all_pass = True
    
    # 测试用例1
    print()
    all_pass &= test_case(
        2871559,
        [5, 4, 2, 6, 0, 1, 3, 7],
        [0, 6, 5, 3, 7, 1, 2, 4]
    )
    
    # 测试用例2
    print()
    all_pass &= test_case(
        1432039,
        [4, 5, 6, 3, 1, 2, 0, 7],
        [0, 5, 6, 7, 4, 2, 3, 1]
    )
    
    print()
    print("=" * 60)
    if all_pass:
        print("所有测试通过!")
    else:
        print("部分测试失败!")
    print("=" * 60)


if __name__ == "__main__":
    main()
