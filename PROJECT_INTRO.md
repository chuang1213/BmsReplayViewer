# BMV (BMS Viewer) — 项目接手介绍

> 生成日期: 2026-06-03
> 语言: C++17 | 构建: CMake 3.20+ | 平台: Windows (MSVC) / Linux (GCC/Clang)
> 总代码量: ~4,660 行 (src) | 版本: v0.3.1 | 当前阶段: Phase 3.0.1

---

## 1. 一句话定位

BMV 是一个 **BMS 谱面分析工具**——解析 BMS/BME 谱面文件，提供静态 PNG 渲染与实时 GPU 交互视图，并叠加 Beatoraja (.brd) 和 LR2 (.lr2rep) 回放数据进行判定/时序分析。

**它不是游戏，也不是谱面编辑器。**

---

## 2. 技术栈一览

| 层级 | 技术选型 |
|------|---------|
| **语言** | C++17 |
| **构建系统** | CMake 3.20+ (FetchContent 拉取 GLFW + ImGui) |
| **编译器** | MSVC (Windows) / GCC / Clang (Linux) |
| **窗口系统** | GLFW 3.4 |
| **GUI 框架** | Dear ImGui (docking 分支)，ImDrawList GPU 直刷 |
| **图形 API** | OpenGL 3.2 Core |
| **PNG 输出** | stb_image_write.h (header-only) |
| **JSON 解析** | nlohmann/json.hpp (header-only) |
| **GZIP 解压** | miniz (tinfl) — 源码编译 |
| **哈希** | picosha2 (SHA256, header-only) + md5 (自实现, header-only) |
| **视频导出** | FFmpeg 管道调用（外部进程，非链接） |
| **RNG** | 手写 MT19937 (1998版)，用于 LR2 Random 模式兼容 |
| **持久化** | JSON 配置文件 (`recent_files.json`)，无数据库 |

**零运行时依赖库**（所有第三方库要么 header-only 要么源码编译进二进制），分发仅需一个 `bmv.exe`。

---

## 3. 核心功能

| 功能 | 说明 |
|------|------|
| BMS/BME 解析 | 完整支持 BMS/BME 格式（含变长度小节、LNOBJ、STOP、BGM、Ch03 HEX BPM） |
| Timeline 数据模型 | 将解析结果统一为不可变 Timeline 容器，含 Note/BPM/STOP/MeasureLine/BGM |
| Tick↔秒 双向转换 | O(log N) 时间映射，正确处理 BPM 变化和 STOP |
| 静态 PNG 渲染 | CLI 模式，IIDX 标准 lane 配色、网格线、BPM/STOP 标记、LN 体+尾 |
| GPU 交互视窗 | GUI 模式，滚动/缩放/裁剪，1P/2P 布局实时切换，Note Speed/Thickness 可调 |
| .brd 回放解析 | Beatoraja 格式：GZIP→JSON→base64→GZIP→9字节帧→按键匹配状态机 |
| .lr2rep 回放解析 | LR2 格式：12字节 LE 记录，操作码分类，op210 判定追踪 |
| LR2 Random 映射 | 手写 MT19937 RNG + Fisher-Yates + 逆置换，精确复现 LR2 lane 映射 |
| 判定分析引擎 | 游标匹配模型，双判定系统（LR2 + Beatoraja），Easy/Normal/Hard/VeryHard（LR2 op210 逐档对照全等） |
| 回放叠加 | Line / Box / Marker 三种渲染模式 + FAST/SLOW 标注 + MISS 标记 |
| FFmpeg 视频导出 | 管道输出 MP4，支持绿幕背景、自定义分辨率/FPS |
| 拖放加载 | 直接拖拽 .bms/.bme/.brd/.lr2rep 到窗口，自动识别 |
| 配置持久化 | 自动保存/恢复最近文件列表和 UI 设置 |
| Hash 校验 | BMS 文件 SHA256/MD5 与回放文件名匹配验证 |

---

## 4. 架构总览

```
                     BMS/BME 文件          .brd / .lr2rep 文件
                         |                       |
                    BmsParser              BrdParser / Lr2RepParser
                         |                       |
                    RawChartData             ReplayData
                         |                       |
                    build_timeline                |
                         |                       |
                       Timeline                   |
                      /       \                  |
                     v         v                 v
              PngRenderer    ChartView    JudgementEngine (GUI)
                 (CLI)         (GUI)            |
                   |             |              v
                  PNG      ImDrawList GPU    Overlay
                                |
                           VideoExport → FFmpeg Pipe → MP4
```

### 数据流 (单向)
```
Parser → RawChartData → Timeline → Renderer / JudgmentEngine
```
Timeline 是不可变中心模型，没有模块会回写 Timeline。

### 共享渲染核心
`CoreRenderer` 提供抽象 `Viewport` + `PixelBuf` 接口，PngRenderer、ChartView、VideoExporter 三者共用同一套绘制原语（背景/网格/Note/LN/replay框）。

---

## 5. 模块详解

### 5.1 `core/` — 核心数据模型

| 文件 | 行数 | 说明 |
|------|------|------|
| `types.h` | 54 | 所有事件结构体：`NoteEvent`, `BpmEvent`, `StopEvent`, `MeasureLine`, `BgmEvent`, `MeasureInfo`, `BpmSource` |
| `time_map.h/cpp` | 142 | Tick↔秒 O(log N) 双向转换，处理 BPM 变化和 STOP |
| `timeline.h/cpp` | 370 | 统一不可变容器 + `build_timeline()` 工厂函数（含 LNOBJ 配对状态机） |

**关键设计**：
- 时间用 `int64_t` ticks（1 beat = 1920 ticks），避免浮点累积误差
- 每种事件类型独立 vector，非 ECS/SoA，追求类型安全与简洁

### 5.2 `format/` — BMS 解析

| 文件 | 行数 | 说明 |
|------|------|------|
| `raw_data.h` | 41 | 中间数据结构 `RawChartData`：channel → raw line 映射 |
| `bms_parser.h/cpp` | 260 | BMS/BME 文件解析器 |

**解析规则**：
- WAV/BPM 引用用 base-36 索引
- Ch03 BPM 用 HEX 值（非 base-36，按 BMS 规范）
- Ch02 measure length 用十进制
- 支持 Ch01 BGM, Ch03/Ch08 BPM, Ch09 STOP, Ch11-19 Notes
- 支持 LNOBJ、变长度小节

### 5.3 `replay/` — 回放解析

| 文件 | 行数 | 说明 |
|------|------|------|
| `replay.h` | 4 | 聚合头文件（包含 replay_data.h + ireplay_parser.h + brd_parser.h） |
| `replay_data.h` | 49 | `ReplayHit` + `ReplayData` 数据结构 |
| `ireplay_parser.h` | 15 | `IReplayParser` 抽象基类 |
| `brd_parser.h/cpp` | 13+190 | Beatoraja BRD 格式：GZIP→JSON→base64→GZIP→9字节帧→按键配对 |
| `lr2rep_parser.h/cpp` | 12+118 | LR2 .lr2rep 格式：12字节 LE 记录，操作码分类，op210 判定跟踪 |
| `lr2_random.h/cpp` | 23+105 | 手写 MT19937 (1998版) RNG + Fisher-Yates + 逆置换 lane 映射 |
| `base64.h` | 47 | URL-safe Base64 解码 |
| `gzip.h` | 97 | 原生 GZIP 解压（调用 miniz tinfl，FLG-aware header 解析） |

### 5.4 `render/` — 渲染

| 文件 | 行数 | 说明 |
|------|------|------|
| `core_renderer.h` | 214 | 共享渲染核心（Viewport/PixelBuf 抽象 + CoreRenderer 静态原语） |
| `png_renderer.h/cpp` | 403 | 静态 PNG 谱面渲染 (CLI)，IIDX 标准 lane 色 |

### 5.5 `judge/` — 判定窗口

| 文件 | 行数 | 说明 |
|------|------|------|
| `judge_profile.h/cpp` | 75 | 判定时机窗口配置 (Easy/Normal/Hard/VeryHard, LR2 + Beatoraja 双系统) |

### 5.6 `analysis/` — 判定分析

| 文件 | 行数 | 说明 |
|------|------|------|
| `judgement_engine.h/cpp` | 706 | 游标匹配判定引擎，LR2 + Beatoraja 双系统，op210 逐档对照 |

### 5.7 `app/` — GUI 应用

| 文件 | 行数 | 说明 |
|------|------|------|
| `application.h/cpp` | 554 | GLFW 窗口 + ImGui 主循环 + 配置持久化 + Hash 校验 |
| `panels/chart_view.h/cpp` | 696 | GPU 谱面视窗 (ImDrawList 直刷, 滚动/缩放/裁剪, 判定叠加, 1P/2P) |
| `panels/video_export.h/cpp` | 349 | FFmpeg 管道视频导出 |
| `panels/welcome_panel.h/cpp` | 60 | Welcome 页签（独立解耦，通过 WelcomeAction 枚举与 Application 通信） |
| `panels/about_panel.h/cpp` | 44 | About 页签（独立解耦） |

### 5.8 `main.cpp` — 入口

| 文件 | 行数 | 说明 |
|------|------|------|
| `main.cpp` | 146 | 双模式入口：无参 GUI | 有参 CLI（含 Integrity Report） |

**GUI 布局**：
```
┌──────────────────────────────────────────────────┐
│  File  Help                                       │  ← 菜单栏
├──────────────────────────────────────────────────┤
│  [Welcome] [Analyzer] [About]                     │  ← TabBar
├──────────────────────────────────────────────────┤
│  ┌─ ChartPanel (58%) ─┐  ┌─ ControlsPanel (42%)──┐│
│  │                    │  │  [Play/Pause]          ││
│  │  ImDrawList GPU    │  │  Time: x.xx / xx.x s   ││
│  │  直刷:             │  │  ── View ──            ││
│  │  - 小节背景/网格   │  │  Show Replay / 1P/2P   ││
│  │  - Note + LN       │  │  ── Replay Display ──  ││
│  │  - Replay 叠加     │  │  Line / Box / Marker   ││
│  │  - MISS 标记       │  │  Show Releases         ││
│  │                    │  │  ── Appearance ──      ││
│  │  交互:             │  │  Note Speed / Thickness││
│  │  - Wheel: 滚动     │  │  Scroll Distance       ││
│  │  - Ctrl+Wheel: 缩放│  │  Auto Follow Play      ││
│  │  - Space: 播放暂停 │  │  ── Judge System ──    ││
│  │                    │  │  LR2 / beatoraja       ││
│  └────────────────────┘  │  ── Accuracy ──       ││
│                          │  PG/GR/GD/BD/POOR     ││
│                          │  FAST/SLOW + Offset   ││
│                          │  ── Replay Info ──    ││
│                          │  Format / Mode / Seed  ││
│                          │  P1/P2 toggle          ││
│                          │  ── Developer ──       ││
│                          │  Hash / Debug / Judge  ││
│                          └────────────────────────┘│
└──────────────────────────────────────────────────┘
```

---

## 6. 目录结构

```
BmsReplayViewer/
├── CMakeLists.txt              # 构建配置 (FetchContent GLFW 3.4 + ImGui docking)
├── readme.md                   # 首页 (简介 / 缺陷 / Todo)
├── ARCHIVE.md                  # 开发归档 (ADRs / 数据结构 / 验证数据)
├── PROJECT_INTRO.md            # 项目接手介绍 (本文档)
├── debug_2026_06_02.md         # Debug 记录 (LR2 op210 对齐 + beatoraja 窗口修正)
├── src/
│   ├── main.cpp                # 双模式入口
│   ├── core/                   # 核心数据模型 (types / time_map / timeline)
│   ├── format/                 # BMS/BME 解析 (bms_parser / raw_data)
│   ├── replay/                 # 回放解析 (brd / lr2rep / lr2_random / gzip / base64)
│   ├── render/                 # 渲染层 (core_renderer / png_renderer)
│   ├── analysis/               # 判定分析 (judgement_engine)
│   ├── judge/                  # 判定窗口配置 (judge_profile)
│   └── app/                    # GUI 应用 (application / chart_view / video_export)
│       └── panels/             # 面板组件 (welcome / about)
├── libs/                       # Vendored 第三方库
│   ├── stb/stb_image_write.h
│   ├── nlohmann/json.hpp
│   ├── picosha2/picosha2.h
│   ├── md5/md5.h
│   └── miniz/                  # miniz tinfl (GZIP 解压核心)
├── testfiles/                  # 测试谱面和回放文件
└── build/                      # CMake 构建输出
    ├── Release/bmv.exe
    └── Debug/bmv.exe
```

---

## 7. 依赖关系

| 依赖 | 来源 | 用途 |
|------|------|------|
| GLFW 3.4 | FetchContent ZIP | 窗口 + OpenGL context |
| Dear ImGui (docking) | FetchContent ZIP | GUI 框架 |
| stb_image_write.h | 本地 vendored | PNG 编码 |
| nlohmann/json.hpp | 本地 vendored | JSON 解析 (BRD) |
| miniz (tinfl) | 本地 vendored 源码编译 | GZIP 解压 |
| picosha2 | 本地 vendored header-only | SHA256 哈希 |
| md5 | 本地 vendored header-only | MD5 哈希 |
| OpenGL32 | 系统链接 (Windows) | GPU 渲染 |
| FFmpeg | 外部进程 | 视频导出（非链接） |

---

## 8. 构建与运行

### 构建

```powershell
# 配置 (首次自动下载 GLFW + ImGui, 约 30s)
cmake -B build -G "Visual Studio 17 2022" -A x64

# Release 构建
cmake --build build --config Release

# Debug 构建（自动运行 LR2Random seed=24332 黄金测试）
cmake --build build --config Debug
```

### 运行

```powershell
# GUI 模式（无参数）
.\build\Release\bmv.exe

# CLI: 谱面 → PNG
.\build\Release\bmv.exe testfiles\air7god.bme output.png

# CLI: 谱面 + 回放叠加 → PNG
.\build\Release\bmv.exe testfiles\air7god.bme output.png --replay testfiles\air7god.bme_Normal.brd

# CLI: 复杂谱面（含 BPM 变化、STOP、LNOBJ）
.\build\Release\bmv.exe "testfiles\L'ouvreur(SPpp).bml" output.png

# CLI: LR2 回放判断（自动输出 op210 对照表）
.\build\Release\bmv.exe testfiles\anata_g24.bme out.png --replay testfiles\eeff3a9a...lr2rep
```

### 中国大陆镜像

如果 GitHub 不可达，编辑 `CMakeLists.txt` 将 URL 中的 `github.com` 替换为 `kkgithub.com`。

---

## 9. 关键设计决策

| 决策 | 原因 |
|------|------|
| **int64 ticks** 而非 float 秒 | 精确比较，无浮点累积误差 |
| **独立 event vector** 而非 ECS/SoA | 类型安全，简洁，项目规模不需要 ECS |
| **无解析器插件架构** | 只有 `BmsParser` 一个实现（Rule of Three 原则） |
| **固定 measure 高度渲染** | 这是分析工具，不是游戏模拟器 |
| **GZIP 原生解压** (miniz tinfl, <1ms) | 比 shell out 到 PowerShell (500ms) 快 |
| **ImDrawList GPU 直刷** (而非 FBO/纹理) | 配合裁剪，支持 144Hz+ |
| **FFmpeg 管道** (而非链接 libavcodec) | 避免复杂构建依赖 |
| **TabBar 布局** (而非 Dockspace) | 更简单，防止拖拽破坏 UI |
| **禁用 imgui.ini** | 防止运行时产生杂散配置文件 |
| **手写 MT19937** (1998版) | LR2 使用不同种子乘数和范围计算，`std::mt19937` 不兼容 |
| **两层 lane 映射** | `display_to_bms` (判定侧) + `bms_lane_to_display_` (渲染侧) 严格互逆 |
| **Controls 固定内嵌** | 防止浮动窗口被 Docking 吸入或丢失 |
| **LR2 判定用整数毫秒** | LR2 客户端使用 truncated int ms，浮点精度会导致边界档位翻转 |
| **op210 真值自检** | .lr2rep 自带逐音符判定真值，可交叉验证引擎正确性 |

> 完整的 ADR 列表（30 项）详见 `ARCHIVE.md` §11。

---

## 10. 当前状态与规划

### 已完成
- Phase 1: 基础架构（解析、Timeline、TimeMap、PNG 渲染）
- Phase 2.1–2.4: GUI 框架、视频导出、判定引擎、UI 美化
- Phase 2.5: LR2 Replay 支持（解析、RNG、OFF/MIRROR/RANDOM 映射）
- Phase 2.5.1: 模块化重构 + P1/P2 双玩家 + 共享渲染核心
- Phase 2.5.2: LR2 判定与 op210 真值对齐（7 项全等）
- Phase 3.0.1: Controls 固定内嵌 + Note Speed + Hash 校验 + Welcome/About 解耦 + Developer 面板

### 近期 (v0.4)
- beatoraja 判定系统重构（含 #RANK 4）
- BMS 注释语法支持 (//, ;, /* */)
- 负 BPM 处理 (逆向滚动)
- 元数据显示：#SUBTITLE, #SUBARTIST, #COMMENT, #DIFFICULTY
- 字体重绘 (添加字母支持)
- 传参 + 目录内 hash 筛选

### 中期 (v0.5)
- #BASE 62 进制支持
- .bmson 解析支持 (BmsonParser)
- 5K BMS 布局适配
- 回放解析解耦重构（ADR-22）
- 谱面波形图

### 远期 (v1.0)
- #LNTYPE 1 通道支持 (0x51-0x69)
- #SCROLLxx / #SPEEDxx 渲染支持
- 地雷通道可视化
- DP/Couple Play 支持

---

## 11. 快速上手建议

1. **先通读 `readme.md`** — 了解项目定位、功能、缺陷
2. **理解数据流**：`BmsParser → build_timeline → Timeline` 是整个程序的核心
3. **理解 Timeline**：看 `core/timeline.h` 和 `core/types.h`，所有模块消费 Timeline
4. **理解回放解析**：BRD 有 5 层嵌套解包 (GZIP→JSON→base64→GZIP→binary→状态机)，是复杂度最高的模块
5. **理解 lane 映射**：两层映射 `display_to_bms` / `bms_lane_to_display_` 必须严格互逆
6. **理解判定引擎**：`JudgementEngine::analyze()` 是核心（~306 行），per-lane cursor 模型忠实复刻 LR2
7. **构建然后跑通**：先 `cmake --build build --config Release`，再跑几个 testfiles 里的例子

---

## 12. 参考文档索引

| 文件 | 内容 |
|------|------|
| `readme.md` | 首页（简介 / 缺陷 / Todo） |
| `ARCHIVE.md` | 开发归档（最全面：ADR 30 项 + 数据结构 + 验证数据 + 构建说明） |
| `PROJECT_INTRO_NEW.md` | 本文档 — 项目接手介绍 |
| `debug_2026_06_02.md` | Debug 记录 (LR2 op210 对齐 + beatoraja 窗口修正) |
| `CMakeLists.txt` | 构建配置 (FetchContent GLFW + ImGui) |
