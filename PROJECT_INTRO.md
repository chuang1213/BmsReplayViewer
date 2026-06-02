# BMV (BMS Viewer) — 项目接手介绍文档

> 生成日期: 2026-06-02
> 语言: C++17 | 构建: CMake 3.20+ | 平台: Windows (MSVC) / Linux (GCC/Clang)
> 总代码量: ~4,000 行 (src) | 当前阶段: Phase 2.5.1

---

## 1. 一句话定位

BMV 是一个 **BMS 谱面分析工具**——解析 BMS/BME 谱面文件，提供静态 PNG 渲染与实时 GPU 交互视图，并叠加 Beatoraja (.brd) 和 LR2 (.lr2rep) 回放数据进行判定/时序分析。

**它不是游戏，也不是谱面编辑器。**

---

## 2. 技术栈一览

| 层级 | 技术选型 |
|------|---------|
| **语言** | C++17 |
| **构建系统** | CMake 3.20+ (FetchContent 拉取第三方库) |
| **编译器** | MSVC (Windows) / GCC / Clang (Linux) |
| **窗口系统** | GLFW 3.4 |
| **GUI 框架** | Dear ImGui (docking 分支)，ImDrawList GPU 直刷 |
| **图形 API** | OpenGL 3.2 Core |
| **PNG 输出** | stb_image_write.h (header-only) |
| **JSON 解析** | nlohmann/json.hpp (header-only) |
| **GZIP 解压** | miniz (tinfl) — 源码编译 |
| **视频导出** | FFmpeg 管道调用（外部进程，非链接） |
| **RNG** | 手写 MT19937 (1998版)，用于 LR2 Random 模式兼容 |
| **持久化** | JSON 配置文件 (`recent_files.json`)，无数据库 |

**零运行时依赖库**（所有第三方库要么 header-only 要么源码编译进二进制），分发仅需一个 `bmv.exe`。

---

## 3. 核心功能

| 功能 | 说明 |
|------|------|
| BMS/BME 解析 | 完整支持 BMS/BME 格式（含变长度小节、LNOBJ、STOP、BGM） |
| Timeline 数据模型 | 将解析结果统一为不可变 Timeline 容器，含 Note/BPM/STOP/MeasureLine/BGM |
| Tick↔秒 双向转换 | O(log N) 时间映射，正确处理 BPM 变化和 STOP |
| 静态 PNG 渲染 | CLI 模式，IIDX 标准 lane 配色、网格线、BPM/STOP 标记、LN 体+尾 |
| GPU 交互视窗 | GUI 模式，滚动/缩放/裁剪，1P/2P 布局实时切换，Note 粗细控制 |
| .brd 回放解析 | Beatoraja 格式：GZIP→JSON→base64→GZIP→9字节帧→按键匹配状态机 |
| .lr2rep 回放解析 | LR2 格式：12字节 LE 记录，操作码分类，op210 判定追踪 |
| LR2 Random 映射 | 手写 MT19937 RNG + Fisher-Yates + 逆置换，精确复现 LR2 lane 映射 |
| 判定分析引擎 | 游标匹配模型，双判定系统（LR2 + Beatoraja），Easy/Normal/Hard/VeryHard |
| 回放叠加 | Line / Box / Marker 三种渲染模式 |
| FFmpeg 视频导出 | 管道输出 MP4，支持绿幕背景、自定义分辨率/FPS |
| 拖放加载 | 直接拖拽 .bms /.bme /.brd /.lr2rep 到窗口 |
| 配置持久化 | 自动保存/恢复最近文件列表和 UI 设置 |

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
                      /      \                   |
                     v        v                  v
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

---

## 5. 模块详解

### 5.1 `core/` — 核心数据模型

| 文件 | 说明 |
|------|------|
| `types.h` (43行) | 所有事件结构体：`NoteEvent`, `BpmEvent`, `StopEvent`, `MeasureLine`, `BgmEvent` |
|------|------|
| `time_map.h/cpp` (112行) | Tick↔秒 O(log N) 双向转换，处理 BPM 变化和 STOP |
| `timeline.h/cpp` (326行) | 统一不可变容器 + `build_timeline()` 工厂函数（含 LNOBJ 配对状态机） |

**关键设计**：
- 时间用 `int64_t` ticks（1 beat = 1920 ticks），避免浮点累积误差
- 每种事件类型独立 vector，非 ECS/SoA，追求类型安全与简洁

### 5.2 `format/` — BMS 解析

| 文件 | 说明 |
|------|------|
| `raw_data.h` | 中间数据结构 `RawChartData`：channel → raw line 映射 |
| `bms_parser.h/cpp` | BMS/BME 文件解析器 |

**解析规则**：
- WAV/BPM 引用用 base-36 索引
- Ch03 BPM 用 HEX 值（非 base-36，按 BMS 规范）
- Ch02 measure length 用十进制
- 支持 Ch01 BGM, Ch03/Ch08 BPM, Ch09 STOP, Ch11-19 Notes
- 支持 LNOBJ、变长度小节

### 5.3 `replay/` — 回放解析

| 文件 | 说明 |
|------|------|
| `replay.h` | 公共接口 `IReplayParser` + `ReplayHit` + `ReplayData` 结构体 |
| `brd_parser.h/cpp` | Beatoraja BRD 格式：GZIP→JSON→base64含GZIP→9字节帧→按键配对 |
| `lr2rep_parser.h/cpp` | LR2 .lr2rep 格式：12字节 LE 记录，操作码分类，op210 判定跟踪 |
| `lr2_random.h/cpp` | 手写 MT19937 (1998版) RNG + Fisher-Yates + 逆置换 lane 映射 |
| `base64.h` | URL-safe Base64 解码 |
| `gzip.h` | 原生 GZIP 解压（调用 miniz tinfl） |

**拆包层数**：
```
.brd 文件
  └─ GZIP 解压 → JSON { "note": { "data": "base64..." } }
       └─ base64 解码 → GZIP 解压 → 9字节帧序列
            └─ 按键配对状态机 → ReplayHit[]
```

### 5.4 `render/` — 渲染

| 文件 | 说明 |
|------|------|
| `png_renderer.h/cpp` | 静态 PNG 谱面渲染 (CLI)，IIDX 标准 lane 色 |
| `core_renderer.h` | 核心渲染工具函数 |

### 5.5 `analysis/` — 分析

| 文件 | 说明 |
|------|------|
| `judgement_engine.h/cpp` | 游标匹配判定引擎，LR2 + Beatoraja 双系统 |

### 5.6 `judge/` — 判定窗口

| 文件 | 说明 |
|------|------|
| `judge_profile.h/cpp` | 判定时机窗口配置 (Easy/Normal/Hard/VeryHard) |

### 5.7 `app/` — GUI 应用

| 文件 | 说明 |
|------|------|
| `application.h/cpp` | GLFW 窗口 + ImGui 主循环 + 配置持久化 |
| `panels/chart_view.h/cpp` | GPU 谱面视窗 (ImDrawList 直刷, 滚动/缩放/裁剪) |
| `panels/video_export.h/cpp` | FFmpeg 管道视频导出 |

**GUI 布局**：
```
┌──────────────────────────────────────────┐
│  File  View                              │  ← 菜单栏
├──────────────────────────────────────────┤
│  [Welcome] [Analyzer] [About]            │  ← TabBar
├──────────────────────────────────────────┤
│  Chart View (ImDrawList GPU 直刷)        │
│  - Scroll: 鼠标滚轮                      │
│  - Zoom: Ctrl+滚轮                       │
│  - Culling: 视口外 Note 跳过渲染         │
├──────────────────────────────────────────┤
│  [Controls Panel (floating)]             │
│  - Show Replay: ☑                        │
│  - Layout: 1P / 2P                       │
│  - Status readout                        │
│  [Video Export Panel]                    │
└──────────────────────────────────────────┘
```

---

## 6. 目录结构

```
D:\BMV\BMV\
├── CMakeLists.txt              # 构建配置
├── ARCHIVE.md                  # 详细开发归档文档 (1028行)
├── src/
│   ├── main.cpp                # 双模式入口: GUI(无参数) / CLI(有参数)
│   ├── core/                   # 核心数据模型
│   ├── format/                 # BMS/BME 解析
│   ├── replay/                 # 回放解析
│   ├── render/                 # 渲染层
│   ├── analysis/               # 判定分析
│   ├── judge/                  # 判定窗口配置
│   └── app/                    # GUI 应用
│       └── panels/             # 面板组件
├── libs/                       # Vendored 第三方库
│   ├── stb/stb_image_write.h
│   ├── nlohmann/json.hpp
│   └── miniz/                  # miniz tinfl
├── testfiles/                  # 测试用的谱面和回放文件
├── tests/                      # 测试目录（规划中）
├── build/                      # CMake 构建输出
│   ├── Release/bmv.exe
│   └── Debug/bmv.exe
├── *_*.md                      # 参考文档（LR2/bmj判定系统笔记）
└── output_*.png                # 示例渲染输出
```

---

## 7. 依赖关系

| 依赖 | 来源 | 用途 |
|------|------|------|
| GLFW 3.4 | FetchContent ZIP | 窗口 + OpenGL context |
| Dear ImGui (docking) | FetchContent ZIP | GUI 框架 |
| stb_image_write.h | 本地 vendored | PNG 编码 |
| nlohmann/json.hpp | 本地 vendored | JSON 解析 |
| miniz (tinfl) | 本地 vendored 源码编译 | GZIP 解压 |
| OpenGL32 | 系统链接 (Windows) | GPU 渲染 |
| FFmpeg | 外部进程 | 视频导出（非链接） |

---

## 8. 构建与运行

### 构建

```powershell
# 配置
cmake -B build -G "Visual Studio 17 2022" -A x64

# Release 构建
cmake --build build --config Release

# Debug 构建（会自动运行 LR2Random seed=24332 黄金测试）
cmake --build build --config Debug
```

### 运行

```powershell
# GUI 模式（无参数）
.\build\Release\bmv.exe

# CLI: 谱面 → PNG
.\build\Release\bmv.exe air7god.bme output.png

# CLI: 谱面 + 回放叠加 → PNG
.\build\Release\bmv.exe air7god.bme output.png --replay air7god.bme_Normal.brd

# CLI: 复杂谱面（含 BPM 变化、STOP、LNOBJ）
.\build\Release\bmv.exe "L'ouvreur(SPpp).bml" output.png
```

### 中国大陆镜像

如果 GitHub 不可达，编辑 `CMakeLists.txt` 将 URL 中的 `github.com` 替换为 `kkgithub.com`。

---

## 9. 关键设计决策

| 决策 | 原因 |
|------|------|
| **int64 ticks** 而非 float 秒 | 精确比较，无浮点累积误差 |
| **独立 event vector** 而非 ECS/SOA | 类型安全，简洁，项目规模不需要 ECS |
| **无解析器插件架构** | 只有 `BmsParser` 一个实现（Rule of Three 原则） |
| **固定 measure 高度渲染** | 这是分析工具，不是游戏模拟器 |
| **GZIP 原生解压** (miniz tinfl, <1ms) | 比 shell out 到 PowerShell (500ms) 快 |
| **ImDrawList GPU 直刷** (而非 FBO/纹理) | 配合裁剪，支持 144Hz+ |
| **FFmpeg 管道** (而非链接 libavcodec) | 避免复杂构建依赖 |
| **TabBar 布局** (而非 Dockspace) | 更简单，防止拖拽破坏 UI |
| **禁用 imgui.ini** | 防止运行时产生杂散配置文件 |
| **手写 MT19937** (1998版) | LR2 使用不同种子乘数和范围计算，`std::mt19937` 不兼容 |
| **两层 lane 映射** | `display_to_bms` (判定侧) + `bms_lane_to_display_` (渲染侧) 严格互逆 |

---

## 10. 当前阶段与后续规划

### 当前阶段: Phase 2.5.1
- 核心功能已完成：解析、渲染、回放解析、判定分析
- GUI 基础框架完备：视窗交互、控制面板、视频导出
- 代码量 ~6,200 行

### 可能的方向
- 单元测试补充（`tests/` 目录待填充）
- 更多回放格式支持
- 渲染性能优化（更大谱面）
- 导出格式扩展

---

## 11. 快速上手建议

1. **先通读 `ARCHIVE.md`** — 这是最详细的开发文档 (1028行)
2. **理解数据流**：`BmsParser → build_timeline → Timeline` 是整个程序的核心
3. **理解 Timeline**：看 `core/timeline.h` 和 `core/types.h`，所有模块消费 Timeline
4. **理解回放解析**：BRD 有 3 层嵌套解包 (GZIP→JSON→base64→GZIP→binary)，是复杂度最高的模块
5. **理解 lane 映射**：两层映射 `display_to_bms` / `bms_lane_to_display_` 必须严格互逆
6. **构建然后跑通**：先 `cmake --build build --config Release`，再跑几个 testfiles 里的例子

---

## 12. 参考文档索引

| 文件 | 内容 |
|------|------|
| `ARCHIVE.md` | 项目开发归档（最全面，但仅供参考，以用户和搜索agent的结果为准） |
