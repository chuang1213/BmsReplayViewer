# BMV (BMS Viewer) — 开发归档

> 最后更新: 2026-06-28 | 版本: 0.3.4-beta
> 本文件归档所有开发过程、历史记录、变更日志、重构计划、代码审查及废弃文档。

---

## 目录

1. [架构决策记录 (ADR)](#1-架构决策记录-adr)
2. [核心数据结构](#2-核心数据结构)
3. [BRD/LR2 解析管线](#3-brdlr2-解析管线)
4. [Lane 映射系统](#4-lane-映射系统)
5. [1P/2P 布局映射](#5-1p2p-布局映射)
6. [验证数据](#6-验证数据)
7. [变更日志](#7-变更日志)
8. [Replay 解析层重构计划](#8-replay-解析层重构计划)
9. [代码审查报告](#9-代码审查报告)
10. [领域模型](#10-领域模型)
11. [历史开发计划](#11-历史开发计划)
12. [已完成阶段](#12-已完成阶段)

---

## 1. 架构决策记录 (ADR)

### ADR-1: int64 tick 而非 float second
**决策**: 使用 `tick_t = int64_t` 作为统一时间单位。
**理由**: 精确比较、精确排序、无浮点累加误差。

### ADR-2: 独立 vector 而非 SoA/ECS
**决策**: Timeline 使用独立 `std::vector<XxxEvent>`。
**理由**: 类型安全、简单直观。

### ADR-3: 无 Parser 插件架构
**决策**: 直接使用具体类 `BmsParser`，无 `IParser` 接口。
**理由**: Rule of Three。

### ADR-4: Ch03 必须 HEX 解析
**决策**: Channel 03 的值用 16 进制解析。
**理由**: BMS 规范。此前误用 base-36 导致错误值。

### ADR-5: Renderer 固定小节高度
**决策**: Renderer 不根据 BPM 或 STOP 缩放小节高度。
**理由**: 谱面分析器而非游戏模拟器。

### ADR-6: LNOBJ 状态机
**决策**: per-channel pending head 模型。跨小节持久化。
**理由**: 符合 LNOBJ 规范，0 orphan 验证通过。

### ADR-7: Replay 采用 IReplayParser 抽象基类
**决策**: 定义 `IReplayParser` 虚基类。
**理由**: 支持多种回放格式无需重构调用方。

### ADR-8: Random 渲染由 Renderer 侧控制
**决策**: ReplayHit.lane 保持物理轨道; 渲染层重排谱面 Note。
**理由**: 玩家操作基于物理键位，谱面 Note 应跟随屏幕显示偏移。

### ADR-9: GZIP 原生解压替代系统调用
**决策**: 采用 miniz 的 `tinfl_decompress_mem_to_mem`。
**理由**: 系统调用 500ms → miniz <1ms。

### ADR-10: GZIP 头 FLG 位动态跳过
**决策**: 严格依 FLG 位动态跳过 FEXTRA、FNAME、FCOMMENT、FHCRC。
**理由**: GZIP 头部不总是固定长度。

### ADR-11: GUI 采用 Dear ImGui + GLFW
**决策**: ImGui docking 分支 + GLFW 3.4，FetchContent ZIP 下载。
**理由**: ImGui 为行业标准，GLFW 轻量跨平台。

### ADR-12: GUI 渲染采用 ImDrawList GPU 直刷
**决策**: 使用 `ImGui::GetWindowDrawList()` 原语。
**理由**: 配合裁剪可跑满 144Hz+。

### ADR-13: 1P/2P 布局纯渲染层切换
**决策**: `lane_to_column()` 纯函数，不解耦 Random 乱序。
**理由**: 布局切换是纯视觉变换。

### ADR-14: 双模式入口
**决策**: `argc == 1` → GUI；`argc >= 2` → CLI。后续扩展 `--gui <file>`。
**理由**: CLI 向后兼容，GUI 零参启动。

### ADR-15: 视频导出采用 FFmpeg Pipe
**决策**: `popen` 开启 ffmpeg 子进程，写入 Raw RGB24。
**理由**: 避免重量级视频库依赖。

### ADR-16: TabBar 替代 Dockspace
**决策**: 移除 DockSpace，用 TabBar。
**理由**: Dockspace 下 TabBar 会被吸入浮动窗口。

### ADR-17: 判定引擎游标匹配模型
**决策**: per-lane note 游标，按时间顺序一对一匹配。
**理由**: 最近邻匹配会造成错配。

### ADR-18: Config 持久化至 recent_files.json
**决策**: UI 配置与最近文件列表共存于 `recent_files.json`。
**理由**: 避免额外配置文件。

### ADR-19: 禁用 imgui.ini
**决策**: `io.IniFilename = nullptr`。
**理由**: 避免杂散文件。

### ADR-20: LR2 解析采用 op 码分类
**决策**: 按 `op < 40` 分类，不依赖 `time_ms == 0`。
**理由**: op 码是唯一可靠的分界标准。

### ADR-21: LR2Random 魔改 MT19937 1998 版
**决策**: 手写 MT19937，1998 版 LCG 播种 + Fisher-Yates + inverse permutation。
**理由**: 现代 `std::mt19937` 使用 2002 版，与 LR2 不兼容。

### ADR-22: display_to_bms 由 JudgementEngine 统一管理
**决策**: JudgementEngine 计算并暴露 getter；ChartView 同步逆映射。
**理由**: 统一单一真源避免偏差。

### ADR-23: replay.h 拆分为三个独立头文件
**决策**: 拆分为 `replay_data.h` + `ireplay_parser.h` + `brd_parser.h`。
**理由**: 解除循环依赖。

### ADR-24: CoreRenderer 共享渲染核心
**决策**: 抽取共同渲染逻辑为 `CoreRenderer`，抽象 `Viewport` + `PixelBuf`。
**理由**: 消除 ~200 行重复代码。

### ADR-25: has_random_info / random_mode 双玩家数组
**决策**: 升级为 `bool[2]` / `LR2RandomMode[2]`。
**理由**: 支持双人模式。

### ADR-26: JudgeProfile 模块独立
**决策**: 判定枚举和窗口类抽取为独立模块。
**理由**: 关注点分离。

### ADR-27: Controls 面板固定内嵌
**决策**: 从浮动改为固定内嵌（58% Chart / 42% Controls）。
**理由**: 浮动窗口会被 Docking 吸入。

### ADR-28: Hash 校验文件名包含匹配
**决策**: BRD 检查包含 SHA256，LR2REP 检查包含 MD5。
**理由**: 文件名匹配零侵入解析管线。

### ADR-29: Welcome/About 面板解耦 + Developer 选项
**决策**: 面板独立文件；`#ifdef BMV_DEBUG` 改为 Developer 开关。
**理由**: 面板独立修改无需碰 Application；调试功能无需重编译。

### ADR-30: LR2 判定与 op210 真值对齐
**决策**: 重写 `JudgementEngine::analyze()` 对齐 LR2 客户端逻辑。
**理由**: op210 逐音符对拍验证全等。

### ADR-31: 双字体策略
**决策**: ImGui 默认字体处理 ASCII；系统 CJK 字体仅用于元数据栏。
**理由**: 避免改变全部 UI 风格。

### ADR-32: UTF-8 路径统一处理
**决策**: `fs_util::to_path()` + `CommandLineToArgvW` + `stbi_write_png_to_func`。
**理由**: MSVC `path(string)` 按 ANSI 解释，必须手工转 UTF-16。

### ADR-33: Release/Debug 子系统分离
**决策**: Release `/SUBSYSTEM:WINDOWS`，Debug `/SUBSYSTEM:CONSOLE`。
**理由**: Release 不弹黑窗，Debug 保留控制台。

---

## 2. 核心数据结构

### 时间系统
```
tick_t = int64_t
TPB = 1920 ticks/beat
TICKS_PER_MEASURE = 7680 (4/4 小节)
```

### Timeline 事件
```cpp
struct NoteEvent   { tick_t tick; tick_t end_tick; uint8_t lane; uint16_t wav_index; };
struct BpmEvent    { tick_t tick; double bpm; BpmSource source; };
struct StopEvent   { tick_t tick; double stop_beats; };
struct MeasureLine { tick_t tick; int measure_num; uint8_t num, den; };
struct BgmEvent    { tick_t tick; uint16_t wav_index; };
struct MeasureInfo { int measure; tick_t start_tick, length_tick; bool explicit_length; };
enum class BpmSource : uint8_t { HEADER, CH03, CH08 };
```

### Replay 数据结构
```cpp
enum class ReplayFormat { BRD, LR2REP };
enum class LR2RandomMode { Off = 0, Mirror = 1, Random = 2, SRandom = 3, RRandom = 4 };

struct ReplayHit {
    tick_t  tick_start, tick_end;
    uint8_t lane, raw_keycode;
    double  time_sec;
    bool    is_press;
};

struct ReplayData {
    std::vector<ReplayHit> hits;
    ReplayFormat format = ReplayFormat::BRD;
    bool  has_shuffle = false;
    int   shuffle_pattern[8] = {0,1,2,3,4,5,6,7};
    bool          has_random_info[2] = {false, false};
    LR2RandomMode random_mode[2]     = {LR2RandomMode::Off, LR2RandomMode::Off};
    int           random_seed     = 0;
    std::vector<uint8_t> lr2_judgements;
    int64_t duration_us = 0;
    int     unmatched   = 0;
};
```

### 共享渲染接口
```cpp
struct Viewport {
    int width = 0, height = 0;
    virtual int y_at(tick_t tick) const = 0;
};

struct PixelBuf {
    virtual void fill_rect(int x, int y, int w, int h, uint32_t color) = 0;
};

class CoreRenderer {
    static void draw_background(...);
    static void draw_notes(...);
    static void draw_replay_hits(...);
};
```

---

## 3. BRD/LR2 解析管线

### BRD 解析管线
```
.brd (GZIP) → gzip::decompress → JSON → keyinput (base64) → base64::decode
→ gzip::decompress → 9字节帧 → 按键配对状态机 → ReplayData
```

每 9 bytes 一组:
- byte[0]: int8_t → keycode = abs(val)-1, isPressed = (val > 0)
- byte[1..8]: int64_t LE → timestamp (μs)

### LR2 解析管线
```
.lr2rep (raw binary) → 验证 size%12==0 → 每 12 bytes 一组 (3× int32 LE)
```

- +0 time_ms, +4 op, +8 value
- op < 40: 输入事件（value==1 → press, value==0 → release）
- op 103/153: random_mode
- op 200: random_seed
- op 210: lr2_judgements

---

## 4. Lane 映射系统

### 两级映射
```
第一级: display_to_bms  (JudgementEngine, 用于判定)
第二级: bms_lane_to_display_  (ChartView, 用于渲染, 严格逆映射)
```

### 映射来源

| Replay 类型 | 映射来源 |
|------------|---------|
| BRD (Normal) | identity |
| BRD (Random) | `shuffle_pattern[8]` |
| LR2REP (OFF) | identity |
| LR2REP (MIRROR) | 硬编码 1↔7, 2↔6, 3↔5, 4→4 |
| LR2REP (RANDOM) | `LR2Random(seed)` Fisher-Yates → inverse |

---

## 5. 1P/2P 布局映射

```cpp
static int lane_to_column(int render_lane, bool is_2p) {
    if (!is_2p) return render_lane;
    return (render_lane == 0) ? 7 : (render_lane - 1);
}
```

1P: SC→col0, K1-K7→col1-7
2P: SC→col7, K1-K7→col0-6

---

## 6. 验证数据

### L'ouvreur 审计
```
BPM: 6 events (Ch03+Ch08), all valid
Measure: 22 explicit (Ch02), 59 default
LN: 345 emitted, 0 orphan, 345/345 YY matched
Notes: 836 total (491 normal + 345 LN)
STOP: 5 events, all 0.125 beats
```

### Replay 验证
| 文件 | Hits | Shuffle | 验证 |
|------|------|---------|------|
| air7god_Normal.brd | 2,810 | NO | ✓ |
| air7god_Random.brd | 3,404 | YES | ✓ |

### LR2Random 金标准
- seed=24332, L=7 → {7,1,2,0,6,3,5,4} ✓
- OldBRD seed=2871559, opt=2 → {0,6,5,3,7,1,2,4} ✓

---

## 7. 变更日志

### 0.3.4-beta (2026-06-28)

#### 跨平台路径编码修复
- 新增 `src/util/fs_util.h`：`to_path()` 统一 UTF-8 路径适配
- `main.cpp`：`CommandLineToArgvW` 重获取 UTF-8 argv
- `stbi_write_png_to_func` 替代 `fopen`
- `_wpopen` 替代 `_popen`

#### 字体策略
- ImGui 默认字体处理 ASCII UI
- 系统 CJK 字体仅用于元数据栏日文显示

#### Release/Debug 子系统分离
- Release: `/SUBSYSTEM:WINDOWS` + `WinMain`
- Debug: `/SUBSYSTEM:CONSOLE` + `main`

#### 其他
- `--gui <file>` 模式
- 控制台 UTF-8 设置

### 2026-06-28: Replay 解析层重构
- 阶段6 集成测试完成
- 字体嵌入、Release 显示修复、Hash 校验、谱面元数据显示
- SHA256/MD5 padding bug 修复

### 2026-06-03: 内部阶段 3.0.1
- GUI 布局重构、Note Speed、Hash 校验、面板解耦、Developer 区域

### 2026-06-02: LR2 判定对齐
- 重写判定循环对齐 LR2 客户端
- op210 逐音符对拍验证全等

### 更早阶段
- Phase 1: 基础架构
- Phase 2.1: GUI 框架
- Phase 2.2: 视频导出
- Phase 2.3: 判定引擎
- Phase 2.4: UI 美化
- Phase 2.5: LR2 Replay 支持
- Phase 2.5.1: 模块化重构

---

## 8. Replay 解析层重构计划

### 背景
replay 解析层曾存在数据结构耦合、职责不清、随机算法散乱、错误处理缺失等问题。

### 架构决策
- 统一回放格式 `UnifiedReplay`
- 时间单位 time_us，解析器不依赖 TimeMap
- 随机算法为纯函数库
- 结构化错误处理 `ParseError`
- 新代码放 `src/replay/unified/` 子目录
- 适配层 `unified_to_replay_data` 过渡

### 统一格式
```cpp
struct ReplayEvent { int64_t time_us; uint8_t lane; };
struct ReplayMetadata {
    std::array<int, 8> shuffle_pattern = {0,1,2,3,4,5,6,7};
    std::optional<RandomMode> random_option;
    std::optional<std::vector<uint8_t>> judgements;
    std::optional<int> seed;
    int64_t duration_us = 0;
};
struct UnifiedReplay {
    std::vector<ReplayEvent> press_events, release_events;
    ReplayMetadata metadata;
};
struct ParseError { std::string file_path, stage, reason, context; };
```

### 实现步骤（全部完成 ✅）
1. 类型定义与随机算法纯函数
2. BRD 解析器（新旧版本）
3. LR2REP 解析器
4. 工厂函数 `parse_replay`
5. 适配层 `unified_to_replay_data`
6. 集成测试

### 测试结果
- BRD 新版: 4512 帧、seed=14266788
- BRD 旧版: 3734 事件、shuffle={0,3,7,6,1,2,5,4}
- LR2: 2272+2273 事件
- 真实 BRD 新版: 2256 hits ✓
- 真实 BRD 旧版: 1867 hits ✓
- 真实 LR2: 4545 hits ✓

---

## 9. 代码审查报告

> 审查日期：2026-06-28 | 范围：src/replay/unified/

### 总体评价
架构清晰，新旧代码完全隔离，适配层测试验证一致，无破坏性改动。

### 问题与建议
1. 新旧 random 模块有重复代码（可接受，过渡设计）
2. LR2 random_mode 只填 P1（当前 7K 单人无影响）
3. value != 0 且 != 1 事件被丢弃（与旧代码一致，正确）
4. GZIP 部分数据情况（错误路径已覆盖）
5. BRD 排序策略 stable_sort（BRD 天然有序，风险低）
6. has_shuffle 语义差异（恒等 shuffle 等于无 shuffle，影响极小）

### 结论
代码质量良好，无破坏性问题，可安全接入。

---

## 10. 领域模型

### 核心概念
- **ReplayEvent**: 单个按键事件（time_us, lane）
- **UnifiedReplay**: 统一格式（press_events, release_events, metadata）
- **ReplayMetadata**: shuffle_pattern, random_option, judgements, seed, duration_us
- **RandomAlgorithm**: 纯函数库（seed + 类型 → shuffle_pattern）

### 边界
- 解析器不依赖 TimeMap
- 时间统一为 time_us
- 下游负责 time_us → tick 转换

### 格式差异
- **BRD**: 新版有 laneShufflePattern，旧版有 seed（Java 随机算法）
- **LR2REP**: 有 seed（LR2 随机算法）+ op210 判定记录

### 职责划分
- **解析器**: 文件 → UnifiedReplay，不依赖 TimeMap
- **随机算法**: 纯计算，无副作用
- **下游**: 绘制用 tick（需转换），判定分析用 time_us

---

## 11. 历史开发计划

### 11.1 旧版本 Beatoraja 回放格式支持
- 新版: JSON → keyinput (base64) → GZIP → 9字节帧
- 旧版: JSON → keylog (字典数组) → 直接解析
- 架构: JSON 解析后检测字段类型，新增 `brd_decode_keylog`

### 11.2 清理与字体支持
- 测试文件移至 `test/`，清理 CMake 测试目标
- 字体方案演变为：默认字体 + CJK 独立 ImFont

---

## 12. 已完成阶段

### Phase 1: 基础架构
BMS/BME 解析器、Timeline、TimeMap、PNG 渲染器

### Phase 2.1: GUI 框架
GLFW + ImGui、ChartView、1P/2P 布局

### Phase 2.2: 视频导出
FFmpeg Pipe、可调分辨率/FPS、绿幕

### Phase 2.3: 判定引擎
LR2/beatoraja 双系统、游标匹配、FAST/SLOW

### Phase 2.4: UI 美化
TabBar、Replay Display Mode、Config 持久化

### Phase 2.5: LR2 Replay 支持
Lr2RepParser、LR2Random MT19937、OFF/MIRROR/RANDOM

### Phase 2.5.1: 模块化重构
replay.h 拆分、CoreRenderer、P1/P2 支持、Lane 映射修复

### Phase 3.0.1: GUI 重构
Controls 固定内嵌、Note Speed、Hash 校验、面板解耦、Developer 区域

### Phase 3.0.2: Replay 解析层重构
统一 UnifiedReplay、纯函数随机算法、结构化错误处理、适配层

### 0.3.4-beta: 跨平台路径 + 字体 + 子系统
fs_util 统一路径、CommandLineToArgvW、stbi_write_png_to_func、_wpopen、双字体、Release/Debug 子系统分离、--gui 模式

---

*项目状态: 0.3.4-beta 完成*
