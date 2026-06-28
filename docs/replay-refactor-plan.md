# Replay 解析层重构计划

## 背景与问题

### 现状
BMV 的 replay 解析层存在以下问题：

1. **数据结构耦合**：`ReplayData` 把 BRD 特有的字段（shuffle_pattern）和 LR2REP 特有的字段（random_mode, op210）混在一个 struct 里，不管实际格式是什么

2. **职责不清**：
   - `replay_adapter.cpp` 按 format 做 if-else 分支，BRD 和 LR2REP 的 hit 构建逻辑完全不同但写在同一个函数里
   - `Application` 知道回放格式细节（比如 LR2REP 用 MD5 校验、BRD 用 SHA256 校验）

3. **随机算法散乱**：随机算法散落在各个 parser 里（`java_random.cpp`、`lr2_random.cpp`），跟解析逻辑耦合在一起

4. **错误处理缺失**：解析失败时只返回空数据或 `std::nullopt`，没有详细的错误信息，调试困难

5. **测试效率低**：大部分测试是 AI 写完代码后完整编译出 exe，用户手动打开验证

### 目标
- 重写 replay 解析层，输出统一格式 `UnifiedReplay`
- 解耦随机算法模块
- 改进错误处理，支持结构化错误信息
- 为后续功能铺路（旧版 brd 兼容、多 key 模式等）

---

## 架构决策

### ADR-001: 统一回放格式

**决策**：所有回放格式最终转换为统一的 `UnifiedReplay` 结构

**理由**：
- 下游代码（绘制、分析、导出）不需要关心格式差异
- press/release 事件分类存储，便于下游按需使用（比如只显示按下事件）
- 元数据统一，包含 shuffle_pattern、random_option、judgements、seed

**替代方案**：
- 保留格式特定的数据结构 → 下游需要处理多种格式，复杂度高
- 只输出配对后的 hit 列表 → 丢失 press/release 的区分，无法支持某些分析场景

### ADR-002: 时间单位使用 time_us

**决策**：统一格式的时间单位使用微秒（time_us），解析器不依赖 TimeMap

**理由**：
- 职责分离：解析器只负责"从文件提取事件"，不应该知道 TimeMap 的存在
- 保持原始精度：time_us 是文件里的原始数据，转成 tick 会损失精度
- 下游按需转换：绘制时需要 tick，调用 `time_map.second_to_tick(time_us / 1e6)` 即可

**替代方案**：
- 解析器输出 tick → 解析器需要 TimeMap，跟 BMS 解析耦合

### ADR-003: 随机算法为纯函数库

**决策**：随机算法实现为纯函数，输入 seed + 类型（旧BRD / LR2），输出 shuffle_pattern[8]

**理由**：
- 最简单，最容易测试
- 纯函数没有副作用，线程安全
- 解析器只需要调用一次，不需要缓存

**替代方案**：
- 带状态的计算器 → 过度设计
- 策略模式 → 算法就两种，不需要动态切换

### ADR-004: 结构化错误处理

**决策**：解析失败时返回结构化的 `ParseError`，包含 file_path、stage、reason、context

**理由**：
- agent 可以直接读取字段，不需要解析字符串
- 结构化数据更容易扩展
- 调用者可以选择是否接收错误信息

**替代方案**：
- 只返回 `std::nullopt` → 调试信息丢失
- 返回字符串错误 → 需要解析，不易扩展

### ADR-005: 文件结构采用子目录

**决策**：新代码放在 `src/replay/unified/` 子目录，旧代码保留在原位

**理由**：
- 新代码有清晰的子目录，不会跟旧代码混淆
- 适配层和旧数据结构保留在原位，Application 层继续用
- CMakeLists.txt 只需要加一个子目录的源文件

**替代方案**：
- 在现有 `src/replay/` 目录下重写 → 新旧代码混在一起
- 新建 `src/replay_v2/` 目录 → 路径变长，但也可以

### ADR-006: 适配层过渡

**决策**：写一个适配层 `unified_to_replay_data`，Application 层暂时继续用 `ReplayData`

**理由**：
- 今天聚焦 replay 解析层，Application 层的重构留到以后
- 适配层让 replay 解析层和 Application 层解耦，可以独立测试
- 后续重构 Application 层时，适配层可以逐步删除

**替代方案**：
- 立即改 Application 层，直接用 `UnifiedReplay` → 改动量大，风险高

### ADR-007: shuffle_pattern 统一格式（阶段1发现）

**决策**：`shuffle_pattern[display_lane] = bms_lane`，统一使用 0=scratch, 1-7=keys

**背景**：
- 旧代码中，新 BRD 的 `laneShufflePattern` 是 beatoraja 格式（0-6=keys, 7=scratch），直接存储
- 旧 BRD 的随机算法输出也是 beatoraja 格式，但前端又做了一次坐标转换，导致双重转换
- 旧 BRD 的随机算法本身也有 bug（多余的反转步骤）

**修复**：
- `brd_parser.cpp`：新 BRD 和旧 BRD 都输出统一格式
- `judgement_engine.cpp`、`png_renderer.cpp`：移除多余的坐标转换

---

## 统一格式定义

```cpp
// types.h

struct ReplayEvent {
    int64_t time_us;  // 微秒时间戳
    uint8_t lane;     // 0=scratch, 1-7=keys
};

enum class RandomMode {
    Off = 0,
    Mirror = 1,
    Random = 2,
    SRandom = 3,
    RRandom = 4,
};

struct ReplayMetadata {
    std::array<int, 8> shuffle_pattern = {0, 1, 2, 3, 4, 5, 6, 7};
    std::optional<RandomMode> random_option;
    std::optional<std::vector<uint8_t>> judgements;  // LR2 的 op210
    std::optional<int> seed;                          // 旧 BRD 和 LR2REP 有
    int64_t duration_us = 0;
};

struct UnifiedReplay {
    std::vector<ReplayEvent> press_events;
    std::vector<ReplayEvent> release_events;
    ReplayMetadata metadata;
};

struct ParseError {
    std::string file_path;
    std::string stage;    // "read", "decompress", "json_parse", "version_detect", "extract_events"
    std::string reason;   // "file_not_found", "gzip_corrupted", "missing_field", etc.
    std::string context;  // 具体信息，比如 "missing keyinput and keylog fields"
};
```

---

## 实现步骤

### 阶段 1: 类型定义与随机算法 ✅ 已完成

**目标**：定义统一格式的类型，实现随机算法纯函数

**已完成任务**：
1. ✅ 创建 `src/replay/unified/types.h`，定义 `ReplayEvent`、`ReplayMetadata`、`UnifiedReplay`、`ParseError`
2. ✅ 创建 `src/replay/unified/random.h/cpp`，实现随机算法纯函数 `compute_shuffle_pattern`
3. ✅ 修复 `java_random.cpp` 中 BRD 随机算法的坐标转换 bug
4. ✅ 修复 `brd_parser.cpp`：旧 BRD 现在会调用随机算法计算 shuffle_pattern
5. ✅ 修复 `brd_parser.cpp`：新 BRD 的 `laneShufflePattern` 转换为统一格式
6. ✅ 修复 `judgement_engine.cpp` 和 `png_renderer.cpp`：移除多余的坐标转换
7. ✅ 创建单元测试 `test_unified_random.cpp`，验证 3 个测试用例

**测试用例**：
- OldBRD seed=2871559, opt=2 → {0,6,5,3,7,1,2,4} ✓
- OldBRD seed=1432039, opt=2 → {0,5,6,7,4,2,3,1} ✓
- LR2 seed=24332, L=7 → {7,1,2,0,6,3,5,4} ✓

**发现的额外问题**：
- 旧代码中 `build_brd_random_pattern` 有坐标转换 bug（多余的反转步骤）
- 旧 BRD 解析器没有调用随机算法，导致 shuffle_pattern 始终为默认值
- 前端代码对 shuffle_pattern 做了双重坐标转换

### 阶段 2: BRD 解析器 ✅ 已完成

**目标**：实现 BRD 格式解析，支持新旧两个版本

**已完成任务**：
1. ✅ 创建 `src/replay/unified/brd_parser.h/cpp`
2. ✅ 实现 `parse_brd_new(const nlohmann::json& j)` — 新版 BRD 解析
3. ✅ 实现 `parse_brd_old(const nlohmann::json& j)` — 旧版 BRD 解析
4. ✅ 创建单元测试 `test_brd_parser.cpp`，验证新旧 BRD 解析

**测试文件**：
- 新 BRD: `testfiles/sp/bc1462814a47c6a0e2d6a6779014f78e8f6f94c3b49ab6ecb9a5752d18137e16_088_Normal.brd`
- 旧 BRD: `testfiles/086/086_random_bc1462814a47c6a0e2d6a6779014f78e8f6f94c3b49ab6ecb9a5752d18137e16_1.brd`

### 阶段 3: LR2REP 解析器 ✅ 已完成

**目标**：实现 LR2REP 格式解析

**已完成任务**：
1. ✅ 创建 `src/replay/unified/lr2_parser.h/cpp`
2. ✅ 实现 `parse_lr2(const std::vector<uint8_t>& raw)` — LR2REP 解析（解耦自旧 `lr2rep_parser.cpp`，输出 `UnifiedReplay`）
3. ✅ 创建单元测试 `test_lr2_parser.cpp`

### 阶段 4: 工厂函数 ✅ 已完成

**目标**：实现 `parse_replay` 工厂函数，根据扩展名分派到具体解析器

**已完成任务**：
1. ✅ 创建 `src/replay/unified/parser.h/cpp`
2. ✅ 实现 `parse_replay(const std::string& path, ParseError* error = nullptr)`
3. ✅ 内部逻辑：
   - 根据扩展名分派（`.brd` → BRD 解析，`.lr2rep` → LR2REP 解析）
   - BRD 在工厂里做文件读取、GZIP 解压、版本检测（keyinput vs keylog）
4. ✅ 创建单元测试 `test_parser.cpp`，7 个测试用例全部通过

**测试结果**：
- `.brd` 新版 (keyinput) — 4512 帧、seed=14266788
- `.brd` 旧版 (keylog) — 3734 事件、shuffle={0,3,7,6,1,2,5,4}、seed=11470162
- `.lr2rep` — 2272+2273 事件、duration=143137000us
- 不支持扩展名 — `stage=extension` / `reason=unsupported_extension`
- 文件不存在 — `stage=read` / `reason=file_open_failed`
- `error=nullptr` 安全测试
- LR2REP 缺失文件测试

### 阶段 5: 适配层 ✅ 已完成

**目标**：实现 `unified_to_replay_data` 适配函数，让 Application 层可以继续用 `ReplayData`

**已完成任务**：
1. ✅ 创建 `src/replay/unified/adapter.h/cpp`
2. ✅ 实现 `ReplayData unified_to_replay_data(const UnifiedReplay& replay, const TimeMap& time_map, ReplayFormat format)`
3. ✅ 内部逻辑：
   - BRD: 状态机配对 press/release → ReplayHit，处理 EOF unmatched
   - LR2: 直接转换，填充 random_mode/seed/judgements
4. ✅ 创建单元测试 `test_adapter.cpp`，7 个测试用例全部通过

**测试结果**：
- 真实 BRD 新版: 2256 hits ✓
- 真实 BRD 旧版: 1867 hits ✓
- 真实 LR2: 4545 hits ✓
- 合成 BRD 配对测试
- 合成 BRD EOF unmatched 测试
- 合成 LR2 元数据测试
- 新旧端到端等价性测试

### 阶段 6: 集成测试

**目标**：用真实的测试文件验证端到端功能

**任务**：
1. 用 `testfiles/` 里的 brd 和 lr2rep 样本，跑完整的解析流程
2. 对比新旧版本的输出（`ReplayData`）
3. 手动验证 UI 显示是否正确

**验收标准**：
- 所有测试文件都能正确解析
- UI 显示与旧版本一致
- 错误情况有清晰的错误信息

---

## 测试策略

### 单元测试

**随机算法**：
- 输入已知 seed，验证输出的 shuffle_pattern 是否正确
- 测试边界情况（seed = 0, seed = 负数）

**BRD 解析器**：
- 测试新版 BRD（有 `keyinput` 字段）
- 测试旧版 BRD（有 `keylog` 字段）
- 测试错误情况（文件不存在、GZIP 损坏、JSON 格式错误、缺少关键字段）

**LR2REP 解析器**：
- 测试正常解析
- 测试错误情况（文件不存在、文件大小不是 12 的倍数）

**工厂函数**：
- 测试 `.brd` 和 `.lr2rep` 格式
- 测试不支持的格式
- 测试文件不存在

**适配层**：
- 输入 `UnifiedReplay`，验证输出的 `ReplayData` 是否正确

### 集成测试

用 `testfiles/` 里的真实样本文件，跑完整的解析流程，对比新旧版本的输出。

---

## 未来工作

### 短期（Application 层重构）
- 重构 Application 层，直接使用 `UnifiedReplay`，删除适配层
- 重构 UI 层，支持 press/release 事件的分类显示

### 中期（新功能）
- 旧版 beatoraja 回放兼容（0.8.6 及以前）
- `.bmson` 格式解析支持
- 5K / 9L / 14K BMS 布局适配
- 详细的判定分析和统计（FAST/SLOW 分布、mean/stddev 时序偏移可视化）

### 长期（跨平台）
- macOS 和 Linux 兼容
- 文件对话框抽象层
- 构建系统优化（CMake 跨平台配置）

---

## 参考文档

- `CONTEXT.md` — 领域模型
- `readme.md` — 项目介绍和待办列表
