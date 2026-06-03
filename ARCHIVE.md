# BMV (BMS Viewer) — 开发归档文档

> 最后更新: 2026-06-03
> 语言: C++17 | 构建: CMake 3.20+ | 平台: Windows (MSVC) / Linux (GCC/Clang)
> 总代码量: ~4,660 行 (src) | 版本: 内部阶段 0.3.1 | 模块: core, format, render, replay, judge, analysis, app
> 当前阶段: 内部阶段 3.0.1

## 1. 项目定位

BMV 是一个 **BMS 谱面分析工具**（非游戏本体、非编辑器）。

核心功能：
- 将 BME/BMS 谱面文件解析为统一 Timeline 数据模型
- 渲染为静态 PNG 谱面视图（CLI 模式）
- 实时 GPU 谱面视窗交互（GUI 模式，ImGui + ImDrawList 直刷）
- 支持 Beatoraja (.brd) 和 LR2 (.lr2rep) 回放数据解析与叠加
- 回放判定分析（LR2 / beatoraja 双系统，游标匹配模型）
- 支持 BRD Random / LR2 OFF / MIRROR / RANDOM 模式的 lane 映射
- 支持 1P/2P 布局实时切换（纯渲染层）
- FFmpeg Pipe 视频导出（Green Screen 可选）
- 配置持久化（自动保存/恢复 UI 设置）

```
CLI:  BMS/BME File → Parser → Timeline → PngRenderer → PNG
                                  ↑
         BRD / .lr2rep → Parser → ReplayData

GUI:  BMS/BME File → Parser → Timeline ────→ ChartView (ImDrawList)
                                  ↑                ↑
         BRD / .lr2rep → Parser → ReplayData ──→ JudgementEngine → Overlay
         GLFW Window ← ImGui ← TabBar (Welcome | Analyzer | About)
                             ← Analyzer: [Chart (58%) | Controls (42%)]
                             ← Video Export (FFmpeg pipe)
```

---

## 2. 架构总览

### 数据流（内部阶段 2.1）

```
  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐
  │  BmsParser   │     │  BrdParser   │     │ Lr2RepParser │
  │  (BMS/BME)   │     │  (BRD JSON)  │     │ (.lr2rep)    │
  └──────┬───────┘     └──────┬───────┘     └──────┬───────┘
         │                    │                    │
         ▼                    ▼                    ▼
  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
  │RawChartData   │    │  ReplayData  │    │  ReplayData  │
  └──────┬───────┘    └──────┬───────┘    └──────┬───────┘
         │                    │                    │
         ▼                    │                    │
  ┌──────────────┐            │                    │
  │build_timeline│            │                    │
  │  • MeasureInfo 表         │                    │
  │  • Ch02 变长度小节         │                    │
  │  • LNOBJ 状态机           │                    │
  │  • BPM source 追踪        │                    │
  └──────┬───────┘            │                    │
         │                    │                    │
         ▼                    │                    │
  ┌──────────────┐            │                    │
  │  Timeline    │            │                    │
  │  • notes[]   │            │                    │
  │  • bpm_changes[]          │                    │
  │  • stops[]   │            │                    │
  │  • measures[]│            │                    │
  │  • bgm[]     │            │                    │
  │  • time_map  │            │                    │
  └──────┬───────┘            │                    │
         │                    │                    │
         ├────────────────────┴────────────────────┤
         ▼                    ▼                    ▼
  ┌─────────────────────────────────────┐
  │       PngRenderer (CLI)             │
  │  • 静态 PNG 谱面渲染               │
  │  • 共享 CoreRenderer 绘制原语       │
  └─────────────────────────────────────┘
   ┌─────────────────────────────────────┐
   │       ChartView (GUI)               │
   │  • ImDrawList GPU 直刷              │
   │  • 滚动 (Wheel) + 缩放 (Ctrl+Wheel) │
   │  • 可见性裁剪 (Culling)             │
   │  • Random 乱序 Note 自动重排        │
   │  • Replay 叠加 (Line/Box/Marker)    │
   │  • 1P/2P 布局实时切换              │
   │  • Note Speed 变速滑条              │
   │  • 播放控制 (Play/Pause)            │
   └─────────────────────────────────────┘
   ┌─────────────────────────────────────┐
   │       Controls Panel (内嵌右侧)      │
   │  • View: Show Replay, 1P/2P layout  │
   │  • Replay Display: Line/Box/Marker  │
   │  • Appearance: Note Speed, Thickness│
   │  • Judge System, Accuracy Stats     │
   │  • Replay Info (Format/Mode/Seed)   │
   │  • Developer: Hash Verify, Debug    │
   └─────────────────────────────────────┘
```

### 设计原则

1. **Timeline 为中心** — 所有模块消费统一 Timeline，不直接依赖 Parser
2. **Parser → RawChartData → Timeline** — 单向数据流，无回写
3. **Timeline 不可变** — 构造后只读，线程安全
4. **Renderer 纯渲染** — 不计算 BPM、不缩放小节、不推导语义
5. **BPM source 可追溯** — 每个 BpmEvent 标注来源（HEADER/Ch03/Ch08）
6. **Replay 不扭曲** — 回放数据保持物理轨道位置，谱面 Note 按 shuffle 重排
7. **布局纯渲染层** — 1P/2P 切换仅在 lane→column 映射层处理，解析层无感知
8. **双模式共存** — 无参启动 → GUI（TabBar + 固定 Controls 面板），带参启动 → CLI
9. **配置持久化** — 所有 UI 设置（Note 粗细、滚动距离、Note Speed、回放模式、自动跟随）自动保存/恢复
10. **判定引擎** — LR2 和 beatoraja 双系统判定，支持 Easy/Normal/Hard/VeryHard 四档
11. **LR2 协议兼容** — 魔改 MT19937 (1998版) RNG + Fisher-Yates + inverse permutation 复刻 LR2Random
12. **映射一致性** — display_to_bms 和 bms_to_display_ 由 JudgementEngine 统一管理，ChartView 通过 getter 同步，避免 Note 渲染与 Hit 判定出现 lane 偏差
13. **共享渲染核心** — PngRenderer + ChartView + VideoExporter 共用 CoreRenderer 绘制原语（draw_background / draw_notes / draw_replay_hits），避免代码重复
14. **面板解耦** — Welcome / About 面板独立文件，纯 ImGui 渲染无业务依赖
15. **Hash 校验** — BMS 文件拖入时计算 SHA256 + MD5，回放文件通过文件名包含匹配校验正确性

---

## 3. 模块划分

```
src/
├── core/
│   ├── types.h              ( 54行)  所有事件 struct + BpmSource 枚举 + MeasureInfo
│   ├── time_map.h/cpp       (142行)  Tick ↔ Second 双向转换（BPM+STOP）
│   └── timeline.h/cpp       (370行)  Timeline 容器 + build_timeline 工厂
├── format/
│   ├── raw_data.h           ( 41行)  Parser 输出的中间结构
│   └── bms_parser.h/cpp     (260行)  BMS/BME 解析器 (base-36 + hex 分流)
├── render/
│   ├── core_renderer.h      (214行)  共享渲染核心 (抽象的 Viewport + PixelBuf + CoreRenderer)
│   └── png_renderer.h/cpp   (403行)  PNG 谱面渲染器 + Replay 叠加 + Random 乱序
├── replay/
│   ├── replay.h             (  4行)  聚合头文件 (include 以下三个)
│   ├── replay_data.h        ( 49行)  ReplayHit, ReplayData, ReplayFormat, LR2RandomMode
│   ├── ireplay_parser.h     ( 15行)  IReplayParser 抽象基类
│   ├── brd_parser.h         ( 13行)  BrdParser 声明
│   ├── brd_parser.cpp       (190行)  BRD 解析: GZIP→JSON→base64→GZIP→9-byte frames→State machine
│   ├── lr2rep_parser.h      ( 12行)  Lr2RepParser 声明
│   ├── lr2rep_parser.cpp    (118行)  LR2 解析: 12B 记录→op 分类→Header/Body→ReplayData
│   ├── lr2_random.h         ( 23行)  LR2Random 类 (魔改 MT19937 1998版)
│   ├── lr2_random.cpp       (105行)  播种 + generateMT + temperAll + Fisher-Yates + 硬校验
│   ├── base64.h             ( 47行)  URL-Safe Base64 解码
│   └── gzip.h               ( 97行)  原生 GZIP 解压 (miniz tinfl)
├── judge/
│   └── judge_profile.h/cpp  ( 75行)  判定时机窗口配置 (Easy/Normal/Hard/VeryHard, LR2+Beatoraja)
├── analysis/
│   └── judgement_engine.h/cpp (706行) 回放判定分析 (游标匹配 + display_to_bms 暴露)
├── app/
│   ├── application.h/cpp    (554行)  GLFW 窗口 + ImGui TabBar + 主循环 + Hash 校验
│   └── panels/
│       ├── chart_view.h/cpp (696行)  GPU 谱面视窗 + 交互控件 + 判定叠加 + Developer
│       ├── video_export.h/cpp (349行)  FFmpeg Pipe 视频导出
│       ├── welcome_panel.h/cpp ( 60行)  Welcome 页签渲染 (独立解耦)
│       └── about_panel.h/cpp  ( 44行)  About 页签渲染 (独立解耦)
└── main.cpp                 (146行)  双模式入口: GUI (无参) / CLI (带参)
```

> **注意**: `replay.h` 已从原来的 64 行单体文件拆分为三个独立文件：
> - `replay_data.h` (38行) — 数据结构定义
> - `ireplay_parser.h` (12行) — 抽象接口
> - `brd_parser.h` (10行) — BrdParser 声明
> - `replay.h` (4行) — 聚合头文件，包含以上三个

> **内部阶段 2.5.1 新增**:
> - `src/judge/judge_profile.h/cpp` — 判定窗口独立模块，从 judgement_engine 中抽取
> - `src/render/core_renderer.h` (180行) — 共享渲染核心，被 PngRenderer / ChartView / VideoExporter 复用
>
> **内部阶段 3.0.1 新增**:
> - `src/app/panels/welcome_panel.h/cpp` — Welcome 页签从 application.cpp 解耦为独立面板
> - `src/app/panels/about_panel.h/cpp` — About 页签从 application.cpp 解耦为独立面板
> - `libs/picosha2/picosha2.h` — 自实现 SHA256 header-only 库
> - `libs/md5/md5.h` — 自实现 MD5 header-only 库

第三方库 (vendored / FetchContent):

```
libs/
├── stb/stb_image_write.h         PNG 编码 (header-only)
├── nlohmann/json.hpp             JSON 解析 (BRD)
├── picosha2/picosha2.h           SHA256 哈希 (自实现, header-only)
├── md5/md5.h                     MD5 哈希 (自实现, header-only)
├── miniz/
│   ├── miniz.h / miniz.c          miniz 入口 (v3.1.0 master, header + zlib wrapper)
│   ├── miniz_export.h             MINIZ_EXPORT 宏定义 (自建)
│   ├── miniz_common.h             类型/Macro 定义
│   ├── miniz_tinfl.h / miniz_tinfl.c  tinfl 解压核心 (编译入 bmv)
│   ├── miniz_tdef.h               tdefl 头 (保留, 未编译)
│   └── miniz_zip.h                ZIP 头 (保留, 未编译)

FetchContent (cmake configure 时自动下载):
├── glfw (3.4)                    窗口/OpenGL 上下文 (ZIP ~1.5MB, TIMEOUT 30s)
└── imgui (docking branch)        GUI 框架 (ZIP ~5MB, TIMEOUT 30s)
```

---

## 4. 核心数据结构

### 时间系统

```
tick_t = int64_t
TPB = 1920 ticks/beat
TICKS_PER_MEASURE = 7680 (4/4 小节)

tick → second:  通过 TimeMap 的 BPM+STOP 预计算表 O(log N) 查询
second → tick:  同上
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

约定: `end_tick > tick` = Long Note, `end_tick == tick` = Normal Note.

### Replay 数据结构

```cpp
enum class ReplayFormat { BRD, LR2REP };

enum class LR2RandomMode {
    Off = 0, Mirror = 1, Random = 2, SRandom = 3, RRandom = 4
};

struct ReplayHit {
    tick_t  tick_start;      // press tick (BRD paired; LR2 KeyDown)
    tick_t  tick_end;        // release tick (BRD paired; LR2 = tick_start)
    uint8_t lane;            // display lane (0=SC, 1-7=keys)
    uint8_t raw_keycode;     // original protocol keycode (BRD: 0-7; LR2: op value)
    double  time_sec;        // raw timestamp (BRD: μs/1e6; LR2: ms/1000, canonical time)
    bool    is_press;        // true=KeyDown, false=KeyUp (BRD always true)
};

struct ReplayData {
    std::vector<ReplayHit> hits;          // press + release events (all preserved)
    ReplayFormat format = ReplayFormat::BRD;

    bool  has_shuffle = false;
    int   shuffle_pattern[8] = {0,1,2,3,4,5,6,7};

    bool          has_random_info[2] = {false, false}; // [0]=P1, [1]=P2
    LR2RandomMode random_mode[2]     = {LR2RandomMode::Off, LR2RandomMode::Off};
    int           random_seed     = 0;

    std::vector<uint8_t> lr2_judgements;  // op210: 5=PGREAT..0=MISS

    int64_t duration_us = 0;
    int     unmatched   = 0;
};
```

**关键职责**:
- `time_sec`: BRD 为缓存值；LR2REP 为原始毫秒真值，是 op210 对拍判定的唯一时间基准
- `is_press`: Parser 保留全部 KeyDown/KeyUp 事件；JudgementEngine 和 ChartView 仅消费 press 事件
- `lr2_judgements`: op210 判定序列原样存储，为 内部阶段 2.5.2 对拍验证预留
- `display_to_bms`: 由 JudgementEngine 内部计算并暴露 getter，ChartView 通过此 getter 同步 `bms_lane_to_display_`
- `has_random_info[2]` / `random_mode[2]`: 支持 P1/P2 双玩家独立模式 (内部阶段 2.5.1 扩展)

### 判定窗口数据结构 (内部阶段 2.5.1 新增)

```cpp
enum class JudgeSystem { LR2, Beatoraja };
enum class JudgeRank : uint8_t { VERY_HARD = 0, HARD, NORMAL, EASY };

struct JudgeWindow {
    int pg;
    int gr;
    int gd;
    int bd;       // symmetric BAD (LR2) or max of fast/slow (BEATORAJA)
    int poor;
    int bd_fast;  // maximum early offset for BAD (ms), stored as positive
    int bd_slow;  // maximum late offset for BAD (ms), stored as positive
};
```

### ChartView 配置

```cpp
enum class ReplayDisplayMode { LineOnly, FullHold, Marker };

struct Config {
    bool show_replay  = true;   // replay overlay toggle
    bool is_2p_layout = false;  // 1P (SC left) / 2P (SC right)
};

// 公开可写成员 (Developer 选项)
bool    show_dev_options    = false;   // 开发者选项展开/折叠
bool    show_debug_overlay  = false;   // 调试叠加层显示
bool    hash_verify_enabled = true;    // BMS↔Replay 文件名 hash 校验
std::string hash_verify_status;        // 校验结果文字 ("OK" / "MISMATCH")
float   note_speed_ = 1.0f;            // Note Speed 变速 (0.5x ~ 2.0x)
```

### 共享渲染接口 (内部阶段 2.5.1 新增)

```cpp
struct Viewport {  // 抽象视口接口 (虚拟 y_at())
    int width = 0, height = 0;
    virtual int y_at(tick_t tick) const = 0;
};

struct PixelBuf {  // 抽象像素缓冲区接口 (虚拟 fill_rect())
    virtual void fill_rect(int x, int y, int w, int h, uint32_t color) = 0;
};

class CoreRenderer {  // 纯静态绘制原语，被 PngRenderer / ChartView / VideoExporter 共用
    static void draw_background(...);     // 小节背景 + 网格线 + lane 分隔线 + 判定线
    static void draw_notes(...);          // Note 头 + LN 体 + LN 尾标记
    static void draw_replay_hits(...);    // 空心按键时长框
};
```

---

## 5. BRD 解析管线（内部阶段 1.5）

```
.brd file (GZIP)
  │
  ▼
gzip::decompress()            → JSON string       [miniz tinfl, <1ms, 零外部进程]
  │
  ▼
nlohmann::json::parse()       → JSON object
  │
  ├── "laneShufflePattern"[0] → shuffle_pattern[8] (2D array → flat)
  │
  └── "keyinput" (string)
        │
        ▼
      base64::decode()        → binary (URL-Safe: '-'=62, '_'=63)
        │
        ▼
      gzip::decompress()      → raw binary key stream
        │
        ▼
      每 9 bytes 一组:
        byte[0]: int8_t  → keycode = abs(val)-1, isPressed = (val > 0)
        byte[1..8]: int64_t LE → timestamp (μs)
        │
        ▼
      time_map.second_to_tick(μs / 1e6)  → tick
        │
        ▼
      按键配对状态机 (per-key):
        PRESS  → 记录 start_tick
        RELEASE → 配对生成 ReplayHit (tick_start, tick_end)
        EOF    → 未松开键截断, unmatched++
        │
        ▼
      ReplayData { hits[], shuffle_pattern[] }
```

### IReplayParser 架构

```cpp
class IReplayParser {
    virtual ~IReplayParser() = default;
    virtual ReplayData parse(path, TimeMap&) = 0;
};
class BrdParser    : public IReplayParser { ... };
class Lr2RepParser : public IReplayParser { ... };  // ✓ 内部阶段 2.5
```

### LR2 解析管线（内部阶段 2.5）

```
.lr2rep file (raw binary, NO compression)
  │
  ▼
验证: file_size % 12 == 0
  │
  ▼
每 12 bytes 一组 (3× int32 LE):
  +0  time_ms   (0 in header, monotonically non-decreasing in body)
  +4  op        (< 40 = input event; >= 40 = header/judgement)
  +8  value     (op-dependent)
  │
  ├── op < 40 (input events, 按 op 码分类，不依赖 time_ms):
  │     value == 1 → ReplayHit { is_press = true,  lane = op, time_sec = ms/1e3 }
  │     value == 0 → ReplayHit { is_press = false, lane = op, time_sec = ms/1e3 }
  │
  ├── op 103 / 153 → random_mode (LR2RandomMode)
  ├── op 200       → random_seed
  ├── op 202       → is_autoplay (logged, not used)
  ├── op 210       → lr2_judgements.push_back(value)  (内部阶段 2.5.2 verification)
  └── other        → ignored
  │
  ▼
ReplayData { format=LR2REP, hits[], random_mode, random_seed, lr2_judgements[] }
```

---

## 6. Lane 映射系统（内部阶段 2.5.1 统一）

### 两级映射架构

```
第一级: display_to_bms  (by JudgementEngine)
  display lane → BMS logical lane
  用于: Hit → Note 匹配判定

第二级: bms_lane_to_display_  (by ChartView, synced from JudgementEngine)
  BMS logical lane → display lane
  用于: Note 渲染定位

两者互为严格逆映射，确保 "判定正确 ↔ 显示正确" 一致性。
```

### 映射来源

| Replay 类型 | 映射来源 | 说明 |
|------------|---------|------|
| BRD (Normal) | identity | display == BMS |
| BRD (Random) | `shuffle_pattern[8]` | 由 `compute_lane_mappings()` 重建 |
| LR2REP (OFF) | identity | `random_mode == Off` |
| LR2REP (MIRROR) | 硬编码 | 1↔7, 2↔6, 3↔5, 4→4 |
| LR2REP (RANDOM) | `LR2Random(seed)` | Fisher-Yates → inverse permutation |
| LR2REP (S-RANDOM/R-RANDOM) | 预留 | identity fallback |

### bms_lane_to_display_ 同步机制（内部阶段 2.5.1 关键修复）

```cpp
// chart_view.cpp set_data() — after judge_engine_.compute_lane_mappings():
const int* d2b = judge_engine_.display_to_bms();  // from JudgementEngine
for (int dl = 0; dl < 8; ++dl) {
    int bl = d2b[dl];
    bms_lane_to_display_[bl] = dl;  // inverse: BMS→display
}
```

此修复解决了 LR2 Random/Mirror 模式下 Note 渲染位置与 Hit 判定不一致的 Bug（此前仅 BRD shuffle 被同步，LR2 映射漏同步导致 Note 原地显示但判定正确）。

---

## 7. 1P/2P 布局映射（内部阶段 2.1）

### 核心逻辑

纯渲染层改动，与 Random 乱序完全解耦。`bms_lane_to_display_` 是逻辑层，`lane_to_column` 是屏幕绘制列映射：

```
逻辑管道:  BMS lane → [shuffle?] → render_lane → [1P/2P?] → screen column → X 像素

1P 模式:  SC (lane 0) → col 0     K1-K7 → col 1-7
2P 模式:  SC (lane 0) → col 7     K1-K7 → col 0-6
```

### 映射函数

```cpp
static int lane_to_column(int render_lane, bool is_2p) {
    if (!is_2p) return render_lane;
    return (render_lane == 0) ? 7 : (render_lane - 1);
}
```

### 接入点

| 数据源 | 列计算 |
|--------|--------|
| Note | `column = lane_to_column(bms_lane_to_display_[note.lane], config.is_2p_layout)` |
| ReplayHit | `column = lane_to_column(hit.lane, config.is_2p_layout)` |

两者统一使用 `chart_x0() + column * kLaneWidth` 计算屏幕 X。

---

## 8. 已实现功能

### BMS/BME 解析

| 特性 | 状态 |
|------|------|
| #TITLE / #ARTIST / #GENRE / #BPM / #PLAYLEVEL | ✓ |
| #WAVxx (base-36 索引, 1..ZZ) | ✓ |
| #BPMxx (扩展 BPM 定义) | ✓ |
| #STOPxx (STOP 定义, value/192 beats) | ✓ |
| #LNOBJ (space-separated base-36 values) | ✓ |
| #BMPxx | ✓ 识别忽略 |
| Channel 01 (BGM) | ✓ base-36 |
| Channel 02 (Measure Length, decimal) | ✓ |
| Channel 03 (BPM direct, **HEX**) | ✓ |
| Channel 08 (BPM ref, base-36) | ✓ |
| Channel 09 (STOP ref, base-36) | ✓ |
| Channel 11-19 (Note lanes, base-36) | ✓ |
| LNTYPE 1 / LNOBJ LN pairing | ✓ |
| #RANDOM / #IF / #ENDIF | ✗ |
| LNTYPE 2 (MGQ notation) | ✗ |
| BMSON / PMS 格式 | ✗ |
| 注释语法 (//, ;, /* */) | ✗ |
| #BASE 62 进制 | ✗ |
| 负 BPM (逆向滚动) | ✗ |
| #RANK 4 (VERY EASY) | ✗ |
| SCROLL / SPEED 扩展 | ✗ |

### 进制分流（关键）

| Channel | 进制 | 示例 |
|---------|------|------|
| 02 (Measure Length) | **Decimal** | "0.875" |
| 03 (直接 BPM) | **HEX** | "55"→85, "AA"→170 |
| 08 (#BPMxx 引用) | BASE36 | "01"→1→#BPM01 |
| 09 (#STOPxx 引用) | BASE36 | "01"→1→#STOP01 |
| 11-19, 01 (音符/BGM) | BASE36 | "0G"→16→#WAV0G |

### Timeline 构建

| 特性 | 状态 |
|------|------|
| MeasureInfo 表 (含 Ch02 变长度) | ✓ |
| LNOBJ 长条配对状态机 (per-channel) | ✓ |
| BPM source 追踪 (HEADER/Ch03/Ch08) | ✓ |
| TimeMap Tick↔Second 转换 | ✓ |
| Cross-measure LN | ✓ |
| orphan LNOBJ 统计 | ✓ |

### PNG 静态渲染

| 特性 | 状态 |
|------|------|
| 竖版谱面视图 (measure 纵向) | ✓ |
| Y 轴反转 (底部=开始) | ✓ |
| IIDX 标准 lane 配色 (红/白/蓝交替) | ✓ |
| 4分/8分/16分 grid 线 | ✓ |
| 小节线 + 小节号 (5×7 字体) | ✓ |
| BPM 变更线 (绿色 + 标签) | ✓ |
| STOP 标记线 (黄色 + 标签) | ✓ |
| LN body + tail 标记 | ✓ |
| Replay 空心按键时长框 | ✓ |
| Random 乱序 Note 自动重排 | ✓ |
| 共享 CoreRenderer 绘制原语 | ✓ 内部阶段 2.5.1 |

### GUI 实时渲染 (内部阶段 2.1 → 2.4)

| 特性 | 状态 |
|------|------|
| GLFW 窗口 + OpenGL 3.2 Core | ✓ |
| Dear ImGui (docking branch) | ✓ |
| TabBar 页签模式 (Welcome \| Analyzer \| About) | ✓ |
| 谱面 GPU 直刷 (ImDrawList) | ✓ |
| 判定线 + 小节/拍子网格 | ✓ |
| LN body + tail 标记 | ✓ |
| 可见性裁剪 (Culling) | ✓ |
| 鼠标滚轮滚动 (Scroll, 可调距离 480-7680) | ✓ |
| Ctrl+滚轮缩放 (Zoom) | ✓ |
| Replay 叠加 (Line / Box / Marker 三模式) | ✓ |
| Show Replay 开关 | ✓ |
| Show Releases 开关 (蓝灰色菱形的 LR2 Release 标记) | ✓ |
| 1P/2P 布局实时切换 | ✓ |
| 浮动控制面板 (Controls Window) | ✓ |
| Controls 固定内嵌 Analyzer 右侧 | ✓ 内部阶段 3.0.1 |
| Note Speed 变速滑条 (0.5x ~ 2.0x) | ✓ 内部阶段 3.0.1 |
| Note Thickness 统一调节 (1-20px) | ✓ |
| Auto Follow Playback 开关 | ✓ |
| Config 持久化 (recent_files.json) | ✓ |
| 双模式启动 (GUI / CLI) | ✓ |
| 拖拽加载谱面 + 回放文件 | ✓ |
| File 菜单 + 最近文件列表 | ✓ |
| Hash 校验 (SHA256/MD5 文件名匹配) | ✓ 内部阶段 3.0.1 |
| Developer 设置区域 (Hash Verify, Debug Overlay, Judge Config) | ✓ 内部阶段 3.0.1 |
| Welcome/About 面板独立解耦 | ✓ 内部阶段 3.0.1 |
| 共享 CoreRenderer 绘制原语 | ✓ 内部阶段 2.5.1 |
| 音频播放 | ✗ |
| 播放头 Seek / 拖拽 | ✗ |

### Replay 解析与渲染

| 特性 | 状态 |
|------|------|
| BRD JSON 解析 (nlohmann) | ✓ |
| laneShufflePattern[0] 2D 数组 | ✓ |
| URL-Safe Base64 解码 | ✓ |
| 9-byte 二进制帧解析 | ✓ |
| PRESS-RELEASE 配对状态机 | ✓ |
| EOF 未松开截断 | ✓ |
| IReplayParser 抽象基类 (独立 ireplay_parser.h) | ✓ 内部阶段 2.5.1 重构 |
| replay 模块拆分 (replay.h → replay_data.h + ireplay_parser.h + brd_parser.h) | ✓ 内部阶段 2.5.1 |
| Normal BRD 验证 | ✓ 2,810 hits |
| Random BRD 验证 | ✓ 3,404 hits, shuffle=YES |
| LR2 .lr2rep 支持 | ✓ 12B 小端记录, op 分类, op210 存储 |
| LR2 OFF 模式 | ✓ |
| LR2 MIRROR 模式 | ✓ |
| LR2 RANDOM 模式 | ✓ LR2Random MT19937 |
| LR2 Release 事件保留 | ✓ `is_press` 标记, 全文保存 |
| P1/P2 双玩家支持 (has_random_info[2] / random_mode[2]) | ✓ 内部阶段 2.5.1 |

### 判定分析 (内部阶段 2.3)

| 特性 | 状态 |
|------|------|
| LR2 判定系统 (PGREAT/GREAT/GOOD/BAD/POOR, 4 档) | ✓ |
| beatoraja 判定系统 (非对称 BAD 窗口) | ✓ |
| 游标匹配模型 (per-lane cursor, 防止最近邻错配) | ✓ |
| FAST/SLOW 双轴判定标注 | ✓ |
| Miss 统计 (POOR overlay) | ✓ |
| Mean ± StdDev offset 统计 | ✓ |
| 判定颜色编码 (绿/蓝/红/紫) | ✓ |
| Judge System 实时切换 | ✓ |
| 判定窗口模块独立 (src/judge/judge_profile.h/cpp) | ✓ 内部阶段 2.5.1 重构 |

### 视频导出 (内部阶段 2.2)

| 特性 | 状态 |
|------|------|
| FFmpeg Pipe 导出引擎 | ✓ |
| 可调分辨率 / FPS | ✓ |
| 绿幕背景选项 | ✓ |
| 导出进度 UI | ✓ |
| 离屏渲染 (脱离屏幕刷新率) | ✓ |
| 共享 CoreRenderer 绘制原语 | ✓ 内部阶段 2.5.1 |

### UI 美化 (内部阶段 2.4)

| 特性 | 状态 |
|------|------|
| TabBar 页签模式 (Welcome / Analyzer / About) | ✓ |
| Replay Display Mode (Line / FullHold / Marker) | ✓ |
| Note Thickness 滑块 (1.0-20.0) | ✓ |
| Scroll Distance 滑块 (480-7680 ticks, 1/16~1 小节) | ✓ |
| Auto Follow Playback 开关 | ✓ |
| Config 持久化 (recent_files.json) | ✓ |
| 无 imgui.ini 残留文件 | ✓ |
| Welcome Page Help 入口修复 | ✓ |
| Controls 浮动窗口 | ✓ |
| Scroll Distance 范围修复 | ✓ |

### LR2 Replay 支持 (内部阶段 2.5)

| 特性 | 状态 |
|------|------|
| Lr2RepParser (IReplayParser 实现) | ✓ |
| 12 字节小端记录解析 | ✓ |
| op < 40 输入事件 (按 op 分类，不依赖 time_ms) | ✓ |
| op 103/153 随机模式读取 → LR2RandomMode 枚举 | ✓ |
| op 200 随机种子读取 | ✓ |
| op 202 isAutoplay 读取 | ✓ |
| op 210 判定序列原样存储 (lr2_judgements) | ✓ |
| lr2_op_to_lane() 7K 映射 (Scratch→0, Keys→1-7) | ✓ |
| Press + Release 全事件保存 (is_press 标记) | ✓ |
| time_sec 原始毫秒时间保存 | ✓ |
| 文件拖拽 + 过滤器支持 .lr2rep | ✓ |
| BRD 零侵入兼容 | ✓ |

### LR2 Lane 映射 (内部阶段 2.5.1)

| 特性 | 状态 |
|------|------|
| LR2Random MT19937 (1998 版 LCG pair-fill 播种 + 预计算 temper) | ✓ |
| nextInt(n) = (uint64_t(r) * n) >> 32 (高 32 位乘法取界) | ✓ |
| Fisher-Yates 洗牌 (1-indexed, 7 键道) | ✓ |
| Inverse permutation → display_to_bms | ✓ |
| MIRROR 硬编码映射 (1↔7, 2↔6, 3↔5, 4→4) | ✓ |
| Scratch 不参与洗牌 | ✓ |
| unit test: seed=24332 金标准断言 (Debug 构建自动运行) | ✓ |
| JudgementEngine::display_to_bms() getter | ✓ |
| ChartView 映射同步修复 (bms_lane_to_display_ 逆映射) | ✓ |
| R-RANDOM 枚举预留 | ✓ |
| Replay Info 显示 (Format / Mode / Seed, Controls 底部) | ✓ |
| Marker 显示模式 (实心圆) | ✓ |
| Show Releases 开关 (蓝灰色菱形 Release 标记) | ✓ |

### 内部阶段 2.5.1 重构 (模块化整理)

| 特性 | 状态 |
|------|------|
| replay.h 拆分为 replay_data.h + ireplay_parser.h + brd_parser.h | ✓ |
| 新增 src/judge/judge_profile.h/cpp (判定窗口独立模块) | ✓ |
| 新增 src/render/core_renderer.h (共享渲染核心，消除 PngRenderer/ChartView/VideoExporter 重复代码) | ✓ |
| has_random_info → bool[2] / random_mode → LR2RandomMode[2] (P1/P2 双玩家) | ✓ |
| JudgeSystem / JudgeRank 枚举移至 judge_profile.h | ✓ |
| CoreRenderer 抽象接口 (Viewport + PixelBuf) 统一三种渲染后端 | ✓ |

---

## 9. 验证数据

### 谱面

| 谱面 | 路径 | 特点 |
|------|------|------|
| `air7god.bme` | testfiles/ | 无 BPM change, 无 STOP, 无 LN, 无 Ch02 |
| `anata_g24.bme` | testfiles/ | 附加测试谱面 |
| `L'ouvreur(SPpp).bml` | testfiles/ | BPM(Ch03+Ch08), STOP, LNOBJ YY, Ch02 |

### L'ouvreur 审计结果

```
BPM Source Audit:
  127.5 | Ch08 | YES
    85.0 | Ch03 | YES
   170.0 | Ch03 | YES
    85.0 | Ch03 | YES
  127.5 | Ch08 | YES
  170.0 | Ch03 | YES

Measure Length: 22 explicit (Ch02), 59 default (1.0)
LN: 345 emitted, 0 orphan, 345/345 YY matched
Notes: 836 total (491 normal + 345 LN)
STOP: 5 events, all 0.125 beats
```

### Replay 验证

| 文件 | Hits | Unmatched | Shuffle | 验证 |
|------|------|-----------|---------|------|
| `air7god.bme_Normal.brd` | 2,810 | 0 | NO | ✓ GZIP + 轨道对齐 |
| `air7god.bme_Random.brd` | 3,404 | 0 | YES | ✓ GZIP + Note 乱序, 回放对齐 |
| `bc1462814a...brd` | — | — | — | ✓ 额外 BRD 测试文件 |
| `bc1462814a..._1.brd` | — | — | — | ✓ 额外 BRD 测试文件 |
| `bc1462814a..._2.brd` | — | — | — | ✓ 额外 BRD 测试文件 |
| `eeff3a9a...lr2rep` | — | — | — | ✓ LR2REP MD5 文件名匹配测试 |

### LR2 Replay 验证

| 测试项 | 状态 |
|--------|------|
| `normal.lr2rep` — OFF 模式 | ✓ |
| `random1.lr2rep` — RANDOM mode (seed=24332) | ✓ |
| `random2.lr2rep` — RANDOM mode (2nd seed) | ✓ |
| .lr2rep 文件 size%12==0 校验 | ✓ |
| Record 小端解析 | ✓ |
| time_ms → time_map.second_to_tick() 转换 | ✓ |
| op 103 / 200 / 210 提取 | ✓ |
| LR2Random seed=24332 映射金标准断言 | ✓ Debug 自动通过 |
| Release 事件 is_press=false 保存 | ✓ |
| time_sec 原始 ms 精度保留 | ✓ |
| `random1_3456712.png` — 预期输出对照 | ✓ |
| `random2_5732146.png` — 预期输出对照 | ✓ |

### 测试文件清单 (testfiles/)

```
testfiles/
├── air7god.bme                    # 基础测试谱面
├── air7god.bme_Normal.brd         # Normal BRD 回放
├── air7god.bme_Random.brd         # Random BRD 回放
├── anata_g24.bme                  # 附加测试谱面
├── bc1462814a...brd               # BRD 测试 (SHA256 文件名匹配)
├── bc1462814a..._1.brd            # 变体: 前缀标记
├── bc1462814a..._2.brd            # 变体: 后缀标记
├── bc1462814a..._2/               # 关联数据目录
├── eeff3a9a...lr2rep              # LR2REP 测试 (MD5 文件名匹配)
├── eeff3a9a...png                 # 预期渲染输出
├── eeff3a9a...txt                 # 未知文本
├── L'ouvreur(SPpp).bml            # 复杂谱面 (BPM/STOP/LNOBJ/Ch02)
├── normal.lr2rep                  # LR2 OFF 模式回放
├── normal.png                     # 预期渲染输出
├── random1.lr2rep                 # LR2 RANDOM (seed=24332)
├── random1_3456712.png            # 预期输出
├── random2.lr2rep                 # LR2 RANDOM (2nd seed)
└── random2_5732146.png            # 预期输出
```

---

---

## 11. 架构决策记录 (ADR)

### ADR-1: int64 tick 而非 float second
**决策**: 使用 `tick_t = int64_t` 作为统一时间单位。
**理由**: 精确比较、精确排序、无浮点累加误差。Second 通过预计算的 TimeMap 查询。

### ADR-2: 独立 vector 而非 SoA/ECS
**决策**: Timeline 使用独立 `std::vector<XxxEvent>` 而非统一基类或 SoA。
**理由**: 类型安全、简单直观、各类型数据量差异大，足够性能。

### ADR-3: 无 Parser 插件架构
**决策**: 直接使用具体类 `BmsParser`，无 `IParser` 接口。
**理由**: MVP 只有一种格式。Rule of Three。

### ADR-4: Ch03 必须 HEX 解析
**决策**: Channel 03 的值用 16 进制解析。
**理由**: BMS 规范。此前误用 base-36 导致 185/370 的错误值。

### ADR-5: Renderer 固定小节高度
**决策**: Renderer 不根据 BPM 或 STOP 缩放小节高度。
**理由**: 谱面分析器而非游戏模拟器。

### ADR-6: LNOBJ 状态机
**决策**: per-channel pending head 模型。普通值 → head；LNOBJ → 配对生成 LN 并清除 head；00 → 无操作。跨小节持久化。
**理由**: 符合 LNOBJ 规范，0 orphan 验证通过。

### ADR-7: Replay 采用 IReplayParser 抽象基类
**决策**: 定义 `IReplayParser` 虚基类，`BrdParser` 和 `Lr2RepParser` 分别继承实现。
**理由**: 支持多种回放格式无需重构调用方。Phase 2.5.1 进一步拆分为独立文件 (ireplay_parser.h / replay_data.h / brd_parser.h)。

### ADR-8: Random 渲染由 Renderer 侧控制
**决策**: ReplayHit.lane 保持物理轨道; 渲染层根据 `shuffle_pattern` 构建 `bms_lane_to_display_` 表重排谱面 Note。
**理由**: 玩家操作基于物理键位，谱面 Note 应跟随屏幕显示偏移。避免 Parser 产出扭曲数据。

### ADR-9: GZIP 原生解压替代系统调用 (Phase 2A)
**决策**: 放弃 PowerShell `GZipStream` + puff.h，采用 miniz 的 `tinfl_decompress_mem_to_mem` + 手动 FLG-aware GZIP 头解析。
**理由**: 
- 系统调用约 500ms 延迟 + 写临时文件 → miniz 原生 <1ms
- puff.h 有 Huffman 树冲突 bug（短码阻塞长码的 lookup table 位置）
- miniz 为 battle-tested 工业级实现，支持 raw DEFLATE 直解
- ISIZE 预分配 buffer + TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF 一次解完

### ADR-10: GZIP 头 FLG 位动态跳过
**决策**: 手动解析 10-byte 基础头后，严格依 FLG 的 bit 2/3/4/1 动态跳过 FEXTRA、FNAME（\0 结尾）、FCOMMENT（\0 结尾）、FHCRC。
**理由**: GZIP 头部不总是固定长度，文件名或注释会引入额外字段。

### ADR-11: GUI 采用 Dear ImGui + GLFW
**决策**: ImGui docking 分支 + GLFW 3.4，FetchContent ZIP 下载（非 git clone），TIMEOUT 30s。
**理由**:
- ImGui 为开发者工具行业标准，docking 支持多面板布局
- GLFW 轻量跨平台窗口框架
- ZIP 直下避免 git 协议防火墙问题
- 无链接依赖（除 opengl32.lib on Windows）

### ADR-12: GUI 渲染采用 ImDrawList GPU 直刷
**决策**: 放弃 FBO 离屏渲染/纹理方案，使用 `ImGui::GetWindowDrawList()` 的 `AddRectFilled`/`AddLine`/`AddText` 原语。
**理由**: 音游谱面线/Note 数量庞大，纹理传输延迟高。GPU 直刷配合可见性裁剪（Culling）可轻松跑满 144Hz+。

### ADR-13: 1P/2P 布局纯渲染层切换
**决策**: 新增 `lane_to_column(render_lane, is_2p)` 纯函数，在 Note 和 ReplayHit 的 X 坐标计算前调用。不解耦 Random 乱序、不改动解析层。
**理由**: 
- 布局切换是纯视觉变换，不应污染数据层
- `bms_lane_to_display_` (shuffle) → `lane_to_column` (1P/2P) 形成清晰的两级管线

### ADR-14: 双模式入口
**决策**: `main()` 中 `argc == 1` → GUI 模式；`argc >= 2` → CLI 模式。
**理由**: 保持 CLI 完全向后兼容，GUI 模式零参启动即用。

### ADR-15: 视频导出采用 FFmpeg Pipe 方案 (Phase 2.2)
**决策**: 不引入 libavcodec 等重量级 C++ 视频编码库，也不采用导出 PNG 序列的方案。采用 `popen` 开启系统子进程，直接将离屏渲染的 Raw RGB24 像素流写入标准输入，交由外部 `ffmpeg` 编码为 MP4。
**理由**:
- C++ 视频库 (libavcodec/libx264) 依赖配置极度复杂，容易破坏目前轻量级的 CMake 管线
- PNG 序列导出引发巨量磁盘 I/O 瓶颈
- 管道传输内存数据速度极快，代码侵入性最小，只需用户系统环境存在 ffmpeg 即可
- 离屏渲染脱离屏幕刷新率，支持任意帧率导出

### ADR-16: GUI 采用 TabBar 替代 Dockspace (Phase 2.4)
**决策**: 移除 `ImGui::DockSpaceOverViewport()`，采用 `ImGui::BeginTabBar()` 实现 Welcome / Analyzer / About 三页签布局。
**理由**:
- Dockspace 模式下 TabBar 会被吸入浮动窗口，双击标题栏后整个 UI 消失
- 固定全尺寸 `##MainWorkspace` 窗口 + TabBar 更简单可靠
- 分析工具不需要多窗口自由停靠，页签模式更直观

### ADR-17: 判定引擎采用游标匹配模型 (Phase 2.3)
**决策**: 每个 lane 维护一个 note 游标，按时间顺序一对一匹配 ReplayHit → Note，而非最近邻搜索。
**理由**:
- 最近邻匹配会造成经典错配问题（例: Notes [1000,1100] vs Hits [1080,1110] → 1080 错配 1100）
- 游标模型类似 LR2/IIDX 实现，保证时序一致性
- 支持 LR2 和 beatoraja 双判定系统，可实时切换

### ADR-18: Config 持久化至 recent_files.json (Phase 2.4)
**决策**: UI 配置项 (note_thickness, scroll_distance, replay_display_mode, auto_follow_playback) 与最近文件列表共同存储于 `recent_files.json` 的 `config` 字段。
**理由**:
- 避免引入额外配置文件，简化文件管理
- 启动时自动恢复用户偏好，无需重新调整
- 退出时和加载文件时自动写入

### ADR-19: 禁用 imgui.ini (Phase 2.4)
**决策**: 设置 `io.IniFilename = nullptr` 禁止 ImGui 自动写入 `imgui.ini`。
**理由**: 避免 exe 启动时在运行目录创建额外文件，提升工具的专业度。

### ADR-20: LR2 解析采用 op 码分类而非 time==0 (Phase 2.5)
**决策**: `.lr2rep` 文件按 `op < 40`（输入事件）与 `op >= 40`（Header/判定）分类，不依赖 `time_ms == 0` 切分 Header。
**理由**: LR2 文档明确指出 time_ms==0 的输入事件客观存在（开局第一个按键可能落在 t=0）。op 码是唯一可靠的分界标准。

### ADR-21: LR2Random 实现魔改 MT19937 1998 版 (Phase 2.5)
**决策**: 手写 MT19937 引擎，使用 1998 版 LCG double high-16-bit pair-filling 播种 + 预计算 624 状态 tempering + Fisher-Yates inverse permutation，不使用 C++ 标准库 `std::mt19937`。
**理由**:
- 现代 `std::mt19937` 使用 2002 版 `init_genrand`（乘数 1812433253），与 LR2 的 1998 版（乘数 69069）完全不同
- LR2 取界用 `(uint64_t(r) * n) >> 32`（64 位乘取高 32 位），非 `r % n`
- LR2 存储逆置换（inverse permutation）而非洗牌后的数组

### ADR-22: 回放解析分层重构（计划，工厂1/工厂2 + 统一 ReplayData）
**背景**: 当前 `src/replay/` 把“文件 → 事件”揉在各 parser 里——两 parser 事件模型不一致、`brd_parser` 一个函数干 6 层、lane 映射散落、`ReplayData` 混多格式；brd 下个版本还要兼容旧版回放（§0 待办 2）。

**决策（目标分层）**:
```
File → 工厂1(格式分派) ┬─→ LR2 parser ───────────────┐
                       └─→ 工厂2(brd 版本分派) ┬ 新 ┤─→ 统一 ReplayData → 消费
                                               └ 旧 ┘
```
- **工厂1（文件格式分派）**: 按扩展名/magic 判 `.lr2rep` / `.brd` 选 parser，取代散在 `main` 的 if-else；`IReplayParser` 要么落地成它返回的多态、要么删。
- **工厂2（brd 版本分派）**: brd 兼容旧版回放，新旧流程有重合 → 版本判断 + 公共子流程抽进 brd 工厂，按版本分支。
- **统一内部格式**: 两 parser 的唯一职责 = 转成统一内部 `ReplayData`（统一事件 `{time, lane, down}`）；格式专属元数据（seed/mode/shuffle/op210）分开装。
- **事件模型统一**: brd 现把按下+抬起配对成一个 hit（`is_press` 恒 true）、lr2 是分开的事件 → 统一，下游（LN 收尾等）不再分叉。
- **lane 在各 parser 内归一**: 物理 lane 0–7 对 beatoraja 是 `1234567s`、对 lr2 是 `s1234567`（scratch 位置不同）→ 各 parser 内归一到统一内部约定（如 scratch=0、键=1–7）。
- **拆 brd_parser 巨函数**: 6 层（读/解压/JSON/base64/再解压/帧解码/配对）拆成可单测的小步。
- **按谱面 hash 匹配回放**: 回放与谱面强绑定，用谱面 hash 筛选/匹配对应回放。

**保留**: base64 / gzip / MT19937（lr2_random）等底层解码器正确，不动。
**注意**: brd 无 op210 那样的逐音符真值，重构后无法像 LR2 那样自检；改 brd 事件模型前先固定几条回放的 hit 数/分布做快照兜底。
- 任一环节用错 → Random 映射全部错位 → op210 对拍必然失败

### ADR-22: display_to_bms 由 JudgementEngine 统一管理 (Phase 2.5.1)
**决策**: `JudgementEngine` 内部计算并存储 `computed_display_to_bms_[8]`，暴露 `display_to_bms()` const getter；`ChartView::set_data()` 通过此 getter 同步 `bms_lane_to_display_` 为严格逆映射。
**理由**:
- 此前 BRD shuffle 和 LR2 shuffle 分别在不同位置计算映射，导致 LR2 Random/Mirror 模式下 Note 渲染位置（bms_lane_to_display_）与 Hit 判定映射（display_to_bms）不一致
- 统一单一真源（Single Source of Truth）避免了两端偏差

### ADR-23: replay.h 拆分为三个独立头文件 (Phase 2.5.1)
**决策**: 将原来的单体 `replay.h` (64行) 拆分为 `replay_data.h` (38行, 数据定义) + `ireplay_parser.h` (12行, 抽象接口) + `brd_parser.h` (10行, BrdParser 声明)，`replay.h` 退化为聚合头文件 (4行) 包含三者。
**理由**:
- 解除循环依赖：`IReplayParser` 依赖 `ReplayData`，但不应依赖具体实现类
- 使用者可按需引入：仅需数据结构时只 include `replay_data.h`
- `brd_parser.h` 独立声明使得前向关联更清晰

### ADR-24: CoreRenderer 共享渲染核心 (Phase 2.5.1)
**决策**: 抽取 PngRenderer / ChartView / VideoExporter 中的共同渲染逻辑为 `CoreRenderer` 静态类，使用抽象 `Viewport` + `PixelBuf` 接口解耦具体渲染后端。
**理由**:
- 三种渲染目标 (内存像素/PngBuffer, GPU ImDrawList, 离屏RGB数组) 共享相同的小节背景/网格/lane分隔/Note/LN/replay框 绘制公式
- 消除 ~200 行重复代码
- Viewport 抽象使 y_at() 在 PngRenderer (tpk反算) 和 ChartView (scroll+zoom+culling) 下统一

### ADR-25: has_random_info / random_mode 升级为双玩家数组 (Phase 2.5.1)
**决策**: `ReplayData` 中 `has_random_info` 从 `bool` 升级为 `bool[2]`，`random_mode` 从 `LR2RandomMode` 升级为 `LR2RandomMode[2]`，索引 0=P1, 1=P2。
**理由**: LR2 回放可同时携带 P1 和 P2 的 Random 模式信息，原单值设计无法表达双人模式。

### ADR-26: JudgeProfile 模块独立 (Phase 2.5.1)
**决策**: 将 `JudgeSystem`/`JudgeRank` 枚举和 `JudgeWindow`/`JudgeProfile` 类从 `judgement_engine.h` 中抽取为独立的 `src/judge/judge_profile.h/cpp` 模块。
**理由**:
- 判定窗口配置与判定分析引擎逻辑关注点不同
- 独立模块可在不依赖 JudgementEngine 的情况下被引用
- JudgementEngine 从 439 行膨胀中回收至更合理的 580 行

### ADR-27: Controls 面板从浮动改为固定内嵌 (Phase 3.0.1)
**决策**: 删除 `render_controls_window()` 浮动窗口，在 `render_analyzer_tab()` 中用 `BeginChild` 将 Analyzer 拆分为左右两栏（ChartPanel 58% / ControlsPanel 42%），通过新增 `render_controls_child()` 公开方法内嵌渲染。
**理由**:
- 浮动窗口在 Docking 模式下会被吸入或随意拖动，用户容易丢失 Controls
- 固定布局更符合音游谱面分析器 "Chart + Controls" 的经典双栏范式
- 消除 `Application::run()` 中的条件渲染调用，简化主循环

### ADR-28: Hash 校验采用文件名包含匹配 (Phase 3.0.1)
**决策**: BMS 文件加载时计算原始字节的 SHA256 + MD5；回放文件加载时提取文件名 stem，BRD 检查包含 SHA256（64 位 hex 串）、LR2REP 检查包含 MD5（32 位 hex 串）。使用自实现 header-only 哈希库（picosha2/md5），不引入外部加密依赖。
**理由**:
- 文件名匹配比嵌入式 metadata 提取更简单灵活，支持前后缀标记（如 `1_[sha256].brd`）
- 不需要修改 BRD/LR2REP Parser，零侵入现有解析管线
- 自实现哈希库避免 OpenSSL / Crypto++ 等重量级依赖，保持轻量单文件构建
- 验证确保用户拖入的正确回放文件（避免谱面/回放不匹配的调试误区）

### ADR-29: Welcome/About 面板独立解耦 + Developer 选项集中管理 (Phase 3.0.1)
**决策**: Welcome 和 About 渲染从 `application.cpp` 抽取为独立 `welcome_panel.h/cpp` 和 `about_panel.h/cpp`；原 `#ifdef BMV_DEBUG` 条件编译改为 Developer 区域开关控制。
**理由**:
- 面板独立：修改文案无需触碰 Application 类，关注点分离
- Welcome 按钮通过 `WelcomeAction` 枚举返回值解耦，面板不依赖 Application 内部方法
- Developer 开关替代条件编译：所有调试功能无需重新编译即可切换，对开发者更友好

### ADR-30: LR2 判定与 op210 真值对齐 + beatoraja 窗口 rank 缩放修正 (2026-06-02)
**决策**: 重写 `JudgementEngine::analyze()` 判定循环，完全对齐 LR2 `Scene04_Play.cpp ProcSinglenote` 逻辑，修正 beatoraja 窗口 rank 缩放方向。
**理由**:
- 原判定循环与 LR2 客户端不匹配，经 op210 逐音符对拍验证差距很大
- 五大修正点：① 一次按键最多判一个 note (BAD 后可 chain)；② 空 POOR 仅在 note 在前方 200-1000ms 时产生；③ op10 (搓盘另一方向) 不参与判定；④ LR2 使用整数毫秒判定 (truncate)，浮点精度会导致边界翻档；⑤ beatoraja 窗口表 rank 缩放之前反了 (VERY_HARD 应为 25%，而非 125%)
- 验证：对 `anata_g24.bme` + `.lr2rep` 进行 op210 逐档对照，PGREAT/GREAT/GOOD/BAD/漏判/空POOR/总数 7 项全等
- 详见 `debug_2026_06_02.md`

---

## 12. 第三方依赖

| 库 | 用途 | 方式 | 版本 |
|----|------|------|------|
| stb_image_write.h | PNG 编码 | header-only, vendored | — |
| nlohmann/json.hpp | JSON 解析 (BRD) | single header, vendored | — |
| picosha2 (自实现) | SHA256 哈希 | header-only, vendored | — |
| md5 (自实现) | MD5 哈希 | header-only, vendored | — |
| miniz (tinfl) | GZIP/raw DEFLATE 解压 | source compiled | v3.1.0 master |
| GLFW | 窗口 + OpenGL 上下文 | FetchContent ZIP | 3.4 |
| Dear ImGui | GUI 框架 + 交互 | FetchContent ZIP | docking branch |

**链接依赖**: opengl32.lib (仅 Windows)

---

## 13. 已完成阶段

### Phase 1: 基础架构
- BMS/BME 解析器、Timeline 构建、TimeMap 系统、PNG 渲染器

### Phase 2.1: GUI 框架
- GLFW + ImGui 窗口、ChartView GPU 直刷、Controls 面板、1P/2P 布局

### Phase 2.2: 视频导出
- FFmpeg Pipe 导出引擎、可调分辨率/FPS、绿幕背景、进度 UI

### Phase 2.3: 判定引擎
- LR2/beatoraja 双系统、游标匹配模型、FAST/SLOW 双轴标注、Miss 统计

### Phase 2.4: UI 美化
- TabBar 页签模式、Replay Display Mode、Note Thickness、Scroll Distance、Auto Follow、Config 持久化

### Phase 2.5: LR2 Replay 支持
- Lr2RepParser (12B 小端记录, op 分类, op210 存储)
- LR2Random MT19937 (1998 版 pair-fill 播种 + Fisher-Yates + inverse permutation)
- LR2 OFF / MIRROR / RANDOM 三种模式
- Press + Release 全事件保留 (is_press)
- time_sec 原始毫秒精度 (op210 对拍基准)

### Phase 2.5.1: 模块化重构 + UI 增强 + P1/P2 支持
- __模块化重构__:
  - `replay.h` 拆分为 `replay_data.h` + `ireplay_parser.h` + `brd_parser.h`
  - 新增 `src/judge/judge_profile.h/cpp` (判定窗口独立模块)
  - 新增 `src/render/core_renderer.h` (共享渲染核心，消除 PngRenderer/ChartView/VideoExporter 重复代码)
  - `has_random_info` → `bool[2]` / `random_mode` → `LR2RandomMode[2]` (P1/P2 双玩家)
- __Lane 映射修复__:
  - `display_to_bms` 统一管理 + ChartView 映射同步修复
- __UI 增强__:
  - Marker 显示模式 / Show Releases 开关
  - Controls Replay Info (Format / Mode / Seed)
  - R-RANDOM 枚举预留
- __测试增强__:
  - LR2Random seed=24332 硬校验 (Debug 构建自动通过)
  - `random1_3456712.png` / `random2_5732146.png` 预期输出对照
  - 新增 testfiles: `anata_g24.bme`, `bc1462814a...brd` 系列, `normal.png` 预期输出

### Phase 3.0.1: GUI 重构 + Note Speed + Hash 校验 + Developer 面板
- __GUI 布局重构__:
  - Controls 从浮动窗口改为固定内嵌 Analyzer 右侧 (58% Chart / 42% Controls)
  - `render_controls_inline()` 通过新增 `render_controls_child()` public 方法暴露
  - 删除 `render_controls_window()` 及主循环调用
- __Note Speed 变速__:
  - 新增 `note_speed_` 滑条 (0.5x ~ 2.0x, 默认 1.0x)，影响播放时 tick 推进速率
- __Hash 校验系统__:
  - 新增 `libs/picosha2/picosha2.h` 和 `libs/md5/md5.h` 自实现 header-only 哈希库
  - BMS 文件加载时计算 SHA256 + MD5 (原文节字节)
  - 回放文件加载时提取文件名 stem，BRD 校验包含 SHA256，LR2REP 校验包含 MD5
  - 校验默认开启，支持关闭，结果在 Developer 区域显示 ("OK" / "MISMATCH")
- __面板解耦__:
  - `render_welcome_tab()` → `src/app/panels/welcome_panel.h/cpp`
  - `render_about_tab()` → `src/app/panels/about_panel.h/cpp`
  - Welcome 按钮通过 `WelcomeAction` 枚举返回值与 Application 解耦
- __Developer 设置区域__:
  - Controls 底部新增可折叠 Developer 区域，含警告文字
  - 移出所有 `#ifdef BMV_DEBUG` 条件编译，改为 Developer 开关控制
  - Hash Verification 开关 + 状态显示
  - Show Debug Overlay 开关 (原 `#ifdef BMV_DEBUG`)
  - Judge Config 信息 (原 `#ifdef BMV_DEBUG`)
- __测试增强__:
  - 验证 SHA256 和 MD5 实现与 `certutil` 交叉确认一致

---

## 15. 参考文档索引

| 文件 | 内容 |
|------|------|
| `ARCHIVE.md` | 本文件 — 项目开发归档（最全面） |
| `readme.md` | 项目简介、功能列表、现有缺陷、Todo |
| `PROJECT_INTRO.md` | 项目接手介绍文档 (快速上手用) |
| `debug_2026_06_02.md` | Debug 记录：LR2 判定与 op210 对齐 + beatoraja 窗口修正 |
| `CMakeLists.txt` | 构建配置 (FetchContent GLFW + ImGui) |

> 注：`judge_LR2.md`、`judge_raja.md`、`lr2.md`、`lr2rep_format.md`、`raja.md` 等参考笔记已移除，判定相关信息已整合进源代码注释和 `debug_2026_06_02.md`。

---

*项目状态: 内部阶段 3.0.1 完成 (GUI 布局重构 + Note Speed 变速 + Hash 校验 + Welcome/About 解耦 + Developer 面板)*

