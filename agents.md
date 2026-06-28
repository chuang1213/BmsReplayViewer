# BMV (BMS Viewer) — 项目接手介绍

> 版本: 0.3.4-beta | C++17 | CMake 3.20+ | Windows (MSVC) / Linux (GCC/Clang)

## 1. 定位

BMV 是 BMS 谱面分析工具——解析 BMS/BME 谱面，提供静态 PNG 渲染与实时 GPU 交互视图，叠加 Beatoraja (.brd) 和 LR2 (.lr2rep) 回放数据进行判定/时序分析。

不是游戏，不是谱面编辑器。

## 2. 技术栈

C++17 / CMake (FetchContent GLFW 3.4 + ImGui docking) / OpenGL 3.2 / stb_image_write / nlohmann::json / miniz (GZIP) / picosha2 + md5 / 手写 MT19937

零运行时依赖，分发仅需 bmv.exe。

## 3. 架构

BMS/BME -> BmsParser -> RawChartData -> build_timeline -> Timeline
                                                              /    \
.brd/.lr2rep -> Parser -> ReplayData -> Renderer / JudgementEngine

Timeline 不可变，单向数据流。

## 4. 模块

| 目录 | 说明 |
|------|------|
| core/ | types, time_map, timeline |
| format/ | bms_parser, raw_data |
| replay/ | brd_parser, lr2rep_parser, lr2_random, gzip, base64 |
| render/ | core_renderer, png_renderer |
| analysis/ | judgement_engine |
| judge/ | judge_profile |
| util/ | fs_util (跨平台 I/O), encoding (文本编码) |
| app/ | application, chart_view, video_export, welcome/about panels |

## 5. 入口

三模式：无参 GUI | --gui <file> GUI+预加载 | 有参 CLI
Windows 用 CommandLineToArgvW 重获取 UTF-8 argv

## 6. 关键设计

- int64 ticks（无浮点误差）
- 独立 event vector（类型安全）
- GZIP 原生解压（miniz <1ms）
- ImDrawList GPU 直刷（144Hz+）
- FFmpeg 管道（避免重依赖）
- 手写 MT19937 1998版（LR2 兼容）
- 两层 lane 映射（严格互逆）
- 双字体（默认 ASCII + CJK 仅元数据栏）
- UTF-8 路径统一（to_path()）
- Release/Debug 子系统分离

完整 ADR 详见 archive.md

## 7. 快速上手

1. 通读 readme.md
2. 理解 BmsParser -> build_timeline -> Timeline
3. 理解 BRD 5 层解包
4. 构建：cmake --build build --config Release

## 8. 参考文档

| 文件 | 内容 |
|------|------|
| readme.md | 项目主入口 |
| agents.md | 本文档 |
| archive.md | 开发归档 |