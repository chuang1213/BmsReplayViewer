#!/usr/bin/env python3
"""
LR2 .lr2rep 回放文件解析器
遵循 lr2rep_format.md 规范实现
"""

import struct
import os
from typing import List, Tuple, Dict, Optional
from dataclasses import dataclass
from enum import IntEnum


class JudgeLevel(IntEnum):
    """判定等级编码"""
    PGREAT = 5
    GREAT = 4
    GOOD = 3
    BAD = 2
    POOR = 1
    MISS = 0


class JudgeWindow(IntEnum):
    """判定窗口大小 (毫秒)"""
    # #RANK 0: VERYHARD
    VERYHARD_PG = 12
    VERYHARD_GR = 24
    VERYHARD_GD = 60
    VERYHARD_BD = 200
    VERYHARD_POOR = 1000
    
    # #RANK 1: HARD
    HARD_PG = 15
    HARD_GR = 30
    HARD_GD = 80
    HARD_BD = 200
    HARD_POOR = 1000
    
    # #RANK 2: NORMAL
    NORMAL_PG = 18
    NORMAL_GR = 40
    NORMAL_GD = 100
    NORMAL_BD = 200
    NORMAL_POOR = 1000
    
    # #RANK 3: EASY
    EASY_PG = 21
    EASY_GR = 60
    EASY_GD = 120
    EASY_BD = 200
    EASY_POOR = 1000


@dataclass
class Record:
    """单条记录 (12字节 = 3×int32)"""
    time_ms: int      # 毫秒时间戳
    op: int           # 事件码
    value: int        # 取值


@dataclass
class HeaderSettings:
    """头部设置信息"""
    hispeed: Optional[int] = None           # op100/150 流速
    gauge_option: Optional[int] = None      # op101/151 血条
    lanecover: Optional[int] = None         # op102/152 遮罩
    random_mode: Optional[int] = None       # op103/153 random模式
    hid_sud: Optional[int] = None           # op104/154 HID/SUD
    rand_fix: Optional[int] = None          # op105/155 randFix
    rand_sc: Optional[int] = None           # op106/156 搓盘随机
    assist: Optional[int] = None            # op107/157 assist
    randomseed: Optional[int] = None        # op200 randomseed
    battle: Optional[int] = None            # op201 battle
    is_autoplay: Optional[int] = None       # op202 是否自动游玩
    hsfix: Optional[int] = None             # op203 hsfix
    is_extra: Optional[int] = None          # op204 is_extra
    m_extra: Optional[int] = None           # op205 m_extra
    dpflip: Optional[int] = None            # op206 dpflip
    # 音量设置 (op40-43)
    vol_fx_on: Optional[int] = None
    vol_master: Optional[int] = None
    vol_key: Optional[int] = None
    vol_bgm: Optional[int] = None
    # EQ设置 (op50-57)
    eq_on: Optional[int] = None
    eq_gain: List[int] = None  # 7个值


@dataclass
class InputEvent:
    """输入事件"""
    time_ms: int
    lane: int           # op值 (0-39)
    pressed: bool       # value==1 按下, 0 抬起


@dataclass
class JudgeEvent:
    """判定事件"""
    time_ms: int
    judge: JudgeLevel   # 判定等级


class LR2Random:
    """LR2Random - MT19937 (1998版)"""
    
    N = 624
    M = 397
    MATRIX_A = 0x9908B0DF
    
    def __init__(self, seed: int):
        self.mt = [0] * (self.N + 1)
        self.mtr = [0] * self.N
        self.mti = 0
        self._set_seed(seed)
    
    def _set_seed(self, seed: int):
        """设置种子"""
        seed = seed & 0xFFFFFFFF
        for i in range(self.N):
            self.mt[i] = (seed & 0xFFFF0000) & 0xFFFFFFFF
            seed = ((69069 * seed) + 1) & 0xFFFFFFFF
            self.mt[i] = (self.mt[i] | ((seed & 0xFFFF0000) >> 16)) & 0xFFFFFFFF
            seed = ((69069 * seed) + 1) & 0xFFFFFFFF
        self._generate_mt()
    
    def _generate_mt(self):
        """生成624个随机数并进行tempering"""
        mag01 = [0, self.MATRIX_A]
        
        for kk in range(self.N - self.M):
            y = ((self.mt[kk] & 0x80000000) | (self.mt[kk + 1] & 0x7FFFFFFF)) & 0xFFFFFFFF
            self.mt[kk] = (self.mt[kk + self.M] ^ (y >> 1) ^ mag01[y & 1]) & 0xFFFFFFFF
        
        for kk in range(self.N - self.M, self.N - 1):
            y = ((self.mt[kk] & 0x80000000) | (self.mt[kk + 1] & 0x7FFFFFFF)) & 0xFFFFFFFF
            self.mt[kk] = (self.mt[kk - (self.N - self.M)] ^ (y >> 1) ^ mag01[y & 1]) & 0xFFFFFFFF
        
        y = ((self.mt[self.N - 1] & 0x80000000) | (self.mt[0] & 0x7FFFFFFF)) & 0xFFFFFFFF
        self.mt[self.N - 1] = (self.mt[self.M - 1] ^ (y >> 1) ^ mag01[y & 1]) & 0xFFFFFFFF
        
        for kk in range(self.N):
            y = self.mt[kk]
            y ^= (y >> 11) & 0xFFFFFFFF
            y ^= ((y << 7) & 0x9D2C5680) & 0xFFFFFFFF
            y ^= ((y << 15) & 0xEFC60000) & 0xFFFFFFFF
            y ^= (y >> 18) & 0xFFFFFFFF
            self.mtr[kk] = y & 0xFFFFFFFF
        
        self.mti = 0
    
    def rand_mt(self) -> int:
        """返回下一个随机数"""
        if self.mti >= self.N:
            self._generate_mt()
        r = self.mtr[self.mti]
        self.mti += 1
        return r & 0xFFFFFFFF
    
    def next_int(self, max_val: int) -> int:
        """返回 [0, max_val) 范围内的随机数"""
        r = self.rand_mt()
        # 64位乘法取高32位
        return ((r & 0xFFFFFFFF) * (max_val & 0xFFFFFFFF)) >> 32


def lr2_lane_pattern(seed: int, L: int) -> List[int]:
    """
    计算 LR2 的 lane 置换模式
    返回: inv[i] = 显示lane i 对应的原始lane (1-indexed)
    """
    rng = LR2Random(seed)
    
    # 恒等排列 (1-indexed)
    a = list(range(L + 1))
    
    # Fisher-Yates洗牌
    for i in range(1, L):
        j = i + rng.next_int(L - i + 1)
        a[i], a[j] = a[j], a[i]
    
    # 求逆置换
    inv = [0] * (L + 1)
    for i in range(1, L + 1):
        inv[a[i]] = i
    
    return inv


def format_lane_pattern(pattern_1indexed: List[int]) -> str:
    """
    格式化lane排列为可读字符串
    pattern_1indexed: inv数组，inv[i] = 显示lane i 对应的原始lane (1-indexed)
    返回字符串如 "3271645" 表示: 
        显示1号键 <- 原始3号键
        显示2号键 <- 原始2号键
        显示3号键 <- 原始7号键
        ...
    """
    L = len(pattern_1indexed) - 1
    return ''.join(str(pattern_1indexed[i]) for i in range(1, L + 1))


class LR2ReplayParser:
    """LR2回放文件解析器"""
    
    # 7K 可玩channel映射 (internalKey -> BMS channel)
    # 键1→11, 键2→12, 键3→13, 键4→14, 键5→15, 键6→18, 键7→19, 搓盘→16
    LANE_TO_CHANNEL = {
        1: 11, 2: 12, 3: 13, 4: 14, 5: 15, 6: 18, 7: 19, 0: 16, 10: 16
    }
    
    def __init__(self, filepath: str):
        self.filepath = filepath
        self.records: List[Record] = []
        self.header: HeaderSettings = HeaderSettings(eq_gain=[0]*7)
        self.inputs: List[InputEvent] = []
        self.judges: List[JudgeEvent] = []
        self._lane_pattern_cache: Optional[List[int]] = None
        self._parse()
    
    def _parse(self):
        """解析文件"""
        with open(self.filepath, 'rb') as f:
            data = f.read()
        
        size = len(data)
        if size % 12 != 0:
            raise ValueError(f"文件大小 {size} 不是12的倍数，可能不是有效的.lr2rep文件")
        
        # 读取所有记录
        for i in range(0, size, 12):
            time_ms, op, value = struct.unpack('<iii', data[i:i+12])
            self.records.append(Record(time_ms, op, value))
        
        # 分离头部和正文
        self._parse_header()
        self._parse_body()
    
    def _parse_header(self):
        """解析头部设置 (time_ms == 0 且 op >= 40)"""
        for rec in self.records:
            if rec.time_ms != 0:
                break  # 头部结束
            if rec.op >= 40:
                self._parse_setting(rec.op, rec.value)
    
    def _parse_setting(self, op: int, value: int):
        """解析单个设置项"""
        # P1设置 (100-107)
        if 100 <= op <= 107:
            idx = op - 100
            if idx == 0: self.header.hispeed = value
            elif idx == 1: self.header.gauge_option = value
            elif idx == 2: self.header.lanecover = value
            elif idx == 3: self.header.random_mode = value
            elif idx == 4: self.header.hid_sud = value
            elif idx == 5: self.header.rand_fix = value
            elif idx == 6: self.header.rand_sc = value
            elif idx == 7: self.header.assist = value
        # P2设置 (150-157) - 略过，单打主要用P1
        elif 150 <= op <= 157:
            pass
        # 其他设置
        elif op == 200:
            self.header.randomseed = value
        elif op == 201:
            self.header.battle = value
        elif op == 202:
            self.header.is_autoplay = value
        elif op == 203:
            self.header.hsfix = value
        elif op == 204:
            self.header.is_extra = value
        elif op == 205:
            self.header.m_extra = value
        elif op == 206:
            self.header.dpflip = value
        # 音量
        elif op == 40: self.header.vol_fx_on = value
        elif op == 41: self.header.vol_master = value
        elif op == 42: self.header.vol_key = value
        elif op == 43: self.header.vol_bgm = value
        # EQ
        elif op == 50: self.header.eq_on = value
        elif 51 <= op <= 57:
            self.header.eq_gain[op - 51] = value
    
    def _parse_body(self):
        """解析正文事件 (输入事件和判定事件)"""
        for rec in self.records:
            if rec.time_ms == 0:
                continue
            
            if rec.op < 40:
                # 输入事件
                self.inputs.append(InputEvent(
                    time_ms=rec.time_ms,
                    lane=rec.op,
                    pressed=(rec.value == 1)
                ))
            elif 210 <= rec.op <= 217:
                # 判定事件 (只处理210: P1 SP)
                if rec.op == 210:
                    self.judges.append(JudgeEvent(
                        time_ms=rec.time_ms,
                        judge=JudgeLevel(rec.value)
                    ))
    
    def get_lane_pattern(self, num_lanes: int = 7) -> Tuple[List[int], str]:
        """
        获取lane置换模式
        返回: (display_lane -> original_lane 的映射 (0-indexed), 格式化的字符串)
        """
        if self._lane_pattern_cache is not None:
            return self._lane_pattern_cache, self._pattern_str_cache
        
        if self.header.random_mode != 2:  # 2 = RANDOM
            # 非RANDOM模式返回恒等映射
            identity = list(range(num_lanes))
            self._lane_pattern_cache = identity
            self._pattern_str_cache = ''.join(str(i+1) for i in range(num_lanes))
            return identity, self._pattern_str_cache
        
        if self.header.randomseed is None:
            raise ValueError("回放文件缺少randomseed (op200)")
        
        # 计算置换 (1-indexed)
        inv = lr2_lane_pattern(self.header.randomseed, num_lanes)
        # 转为0-indexed
        pattern = [inv[i+1] - 1 for i in range(num_lanes)]
        pattern_str = format_lane_pattern(inv)
        
        self._lane_pattern_cache = pattern
        self._pattern_str_cache = pattern_str
        return pattern, pattern_str
    
    def get_judge_window(self, rank: int = 2) -> Tuple[int, int, int, int, int]:
        """
        获取判定窗口大小
        rank: 0=VERYHARD, 1=HARD, 2=NORMAL, 3=EASY
        """
        windows = [
            (12, 24, 60, 200, 1000),   # VERYHARD
            (15, 30, 80, 200, 1000),   # HARD
            (18, 40, 100, 200, 1000),  # NORMAL
            (21, 60, 120, 200, 1000),  # EASY
        ]
        return windows[rank]
    
    def summary(self) -> dict:
        """输出解析摘要"""
        judge_counts = {j: 0 for j in JudgeLevel}
        for j in self.judges:
            judge_counts[j.judge] += 1
        
        exscore = judge_counts[JudgeLevel.PGREAT] * 2 + judge_counts[JudgeLevel.GREAT]
        
        # 获取随机排列信息
        random_seed = self.header.randomseed
        random_mode = self.header.random_mode
        pattern_str = None
        pattern_desc = None
        
        if random_mode == 2 and random_seed is not None:
            _, pattern_str = self.get_lane_pattern()
            pattern_desc = f"RANDOM (seed={random_seed}) 排列={pattern_str}"
        elif random_mode == 1:
            pattern_desc = "MIRROR (对称翻转)"
        elif random_mode == 3:
            pattern_desc = "S-RANDOM (逐行随机)"
        elif random_mode == 0:
            pattern_desc = "OFF (无随机)"
        else:
            pattern_desc = f"未知模式 (mode={random_mode})"
        
        return {
            "file": self.filepath,
            "records_total": len(self.records),
            "header_records": len([r for r in self.records if r.time_ms == 0]),
            "body_records": len([r for r in self.records if r.time_ms > 0]),
            "input_events": len(self.inputs),
            "judge_events": len(self.judges),
            "randomseed": random_seed,
            "random_mode": random_mode,
            "random_mode_desc": pattern_desc,
            "lane_pattern": pattern_str,
            "is_autoplay": self.header.is_autoplay,
            "judge_counts": {k.name: v for k, v in judge_counts.items()},
            "exscore": exscore,
        }
    
    def to_text(self) -> str:
        """导出为可读文本"""
        lines = []
        lines.append("=" * 60)
        lines.append(f"LR2 Replay 解析结果: {self.filepath}")
        lines.append("=" * 60)
        
        lines.append("\n[Header Settings]")
        for k, v in self.header.__dict__.items():
            if v is not None and k != 'eq_gain':
                lines.append(f"  {k}: {v}")
        if any(g != 0 for g in self.header.eq_gain):
            lines.append(f"  eq_gain: {self.header.eq_gain}")
        
        lines.append(f"\n[Random Information]")
        if self.header.randomseed is not None:
            lines.append(f"  Random Seed: {self.header.randomseed}")
        lines.append(f"  Random Mode: {self.header.random_mode}")
        if self.header.random_mode == 2 and self.header.randomseed is not None:
            _, pattern_str = self.get_lane_pattern()
            lines.append(f"  Lane Pattern (显示键位 → 原始键位):")
            for i, orig in enumerate(self._lane_pattern_cache):
                lines.append(f"    显示键{i+1} ← 原始键{orig+1}")
            lines.append(f"  排列字符串: {pattern_str}")
        
        lines.append(f"\n[Statistics]")
        lines.append(f"  总记录数: {len(self.records)}")
        lines.append(f"  输入事件数: {len(self.inputs)}")
        lines.append(f"  判定事件数: {len(self.judges)}")
        
        judge_counts = {}
        for j in self.judges:
            judge_counts[j.judge] = judge_counts.get(j.judge, 0) + 1
        lines.append(f"  判定分布:")
        for j in JudgeLevel:
            lines.append(f"    {j.name}: {judge_counts.get(j, 0)}")
        
        exscore = judge_counts.get(JudgeLevel.PGREAT, 0) * 2 + judge_counts.get(JudgeLevel.GREAT, 0)
        lines.append(f"  EX Score: {exscore}")
        
        lines.append("\n[First 20 Input Events]")
        for i, inp in enumerate(self.inputs[:20]):
            action = "按下" if inp.pressed else "抬起"
            lines.append(f"  t={inp.time_ms:6d}ms  lane={inp.lane:2d} {action}")
        
        lines.append("\n[First 20 Judge Events]")
        for i, judge in enumerate(self.judges[:20]):
            lines.append(f"  t={judge.time_ms:6d}ms  {judge.judge.name}")
        
        return "\n".join(lines)


def main():
    import argparse
    
    parser = argparse.ArgumentParser(description="LR2 .lr2rep 回放文件解析器")
    parser.add_argument("file", help="要解析的.lr2rep文件路径")
    parser.add_argument("-t", "--text", action="store_true", help="输出详细文本格式")
    parser.add_argument("-s", "--summary", action="store_true", help="仅输出简洁摘要")
    
    args = parser.parse_args()
    
    if not os.path.exists(args.file):
        print(f"文件不存在: {args.file}")
        return
    
    try:
        replay = LR2ReplayParser(args.file)
        
        if args.text:
            print(replay.to_text())
        elif args.summary:
            summary = replay.summary()
            print(f"文件: {summary['file']}")
            print(f"总记录数: {summary['records_total']}")
            print(f"输入事件: {summary['input_events']}")
            print(f"判定事件: {summary['judge_events']}")
            print(f"随机模式: {summary['random_mode_desc']}")
            if summary['lane_pattern']:
                print(f"Lane排列: {summary['lane_pattern']}")
            print(f"EX Score: {summary['exscore']}")
            print("判定分布:")
            for name, cnt in summary['judge_counts'].items():
                print(f"  {name}: {cnt}")
        else:
            # 默认输出 - 包含seed和排列
            summary = replay.summary()
            print("=" * 50)
            print(f"文件: {summary['file']}")
            print("=" * 50)
            print(f"音符数: {summary['judge_events']}")
            print(f"随机模式: {summary['random_mode_desc']}")
            
            if summary['randomseed'] is not None:
                print(f"Random Seed: {summary['randomseed']}")
            if summary['lane_pattern']:
                print(f"Lane排列 (显示→原始): {summary['lane_pattern']}")
                # 显示详细映射
                pattern, _ = replay.get_lane_pattern()
                print(f"  详细: " + '  '.join([f"{i+1}←{p+1}" for i, p in enumerate(pattern)]))
            
            print(f"\n判定结果:")
            print(f"  PGREAT={summary['judge_counts'].get('PGREAT',0)}  GREAT={summary['judge_counts'].get('GREAT',0)}")
            print(f"  GOOD={summary['judge_counts'].get('GOOD',0)}    BAD={summary['judge_counts'].get('BAD',0)}")
            print(f"  POOR={summary['judge_counts'].get('POOR',0)}    MISS={summary['judge_counts'].get('MISS',0)}")
            print(f"\nEX Score: {summary['exscore']}")
            if summary['is_autoplay']:
                print("⚠️  此回放为自动游玩 (Autoplay)")
    
    except Exception as e:
        print(f"解析失败: {e}")
        import traceback
        traceback.print_exc()


if __name__ == "__main__":
    main()