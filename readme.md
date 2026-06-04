## 简介

BMV (BMS Visualizer) 是一款 BMS 节奏游戏回放分析工具。导入谱面(.bms/.bme/.bml)和回放文件(.brd/.lr2rep)后，可以逐帧复盘演奏、对比判定差异、导出分析视频。

核心功能：
- 实时 GPU 渲染谱面视图 (ImGui + OpenGL)
- 回放按键叠加显示，支持 Line / Box / Marker 三种模式
- LR2 与 beatoraja 双判定系统，支持判定统计
- FAST/SLOW 标注与 mean/stddev 时序偏移（待进一步开发）
- 静态 PNG 导出 (CLI残留)
- FFmpeg 管道视频导出
- 拖放加载 (自动识别谱面/回放)
- SHA256/MD5 哈希校验

（这是我的第一个开源项目，有很多不到位的地方还请多指教。）

## 现有缺陷
- 旧版本 oraja 回放不支持（仅支持 0.8.7+）
- .bmson 格式不知道支不支持
- 不支持 BMS 注释语法（//、;、/* */），含注释的谱面可能解析失败
- Channel 03 HEX BPM 丢弃了负值，不支持逆向滚动谱面
- #BASE 62 进制未支持，使用 62 进制的谱面 #WAV/#BPM 索引会解析错误
- 仅支持7k
- 内嵌像素字体缺少字母，BPM/STOP 标签无法显示完整
- 分开拖谱和回放有点麻烦

## 构建与使用

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


## 回放解析架构

### 三层模型
回放解析采用三层流水线设计：

1. **文件读取层**（`brd_parser.cpp` / `lr2rep_parser.cpp`）
   - 负责读取原始文件、解压缩、格式解码
   - BRD: GZIP → JSON → base64 → GZIP → 9字节帧流
   - LR2REP: 直接读取 12字节 LE 记录流
   - 输出统一的 `RawInputEvent` 列表（press/release 分离，不配对）

2. **RawInputEvent 转换层**（`replay_adapter.cpp`，临时适配层）
   - 将 `RawInputEvent` 转换为 legacy `ReplayData`
   - BRD 路径：状态机按 lane 配对 press/release → `ReplayHit`
   - LR2 路径：每个事件直接转为一个 `ReplayHit`
   - 通过 `TimeMap` 将微秒时间戳转换为 tick

3. **TimeMap 映射层**（`judgement_engine.cpp`）
   - `compute_lane_mappings()` 在此处执行 shuffle/random 映射
   - 将显示 lane 映射到 BMS 通道 lane

### ReplayInput vs ReplayData
- **`ReplayInput`**：新接口，包含 `RawInputEvent` + 元数据（`Lr2Meta`/`BrdMeta`），不依赖 `TimeMap`
- **`ReplayData`**：旧接口，包含 `ReplayHit`（已配对、已转换 tick），依赖 `TimeMap`
- 下游消费者未来应直接使用 `ReplayInput`，`replay_adapter` 为 Phase 3 删除候选

### 格式差异统一方式
- BRD 和 LR2REP 的事件模型不同（BRD 为 base64+GZIP 压缩帧流，LR2 为原始二进制记录），但统一输出为 `RawInputEvent`
- 物理 keycode → 统一显示 lane 的映射在各自 parser 内完成

### Lane 坐标系
- `RawInputEvent.lane` 和 `ReplayHit.lane` 均使用统一显示 lane
- **0 = scratch（转盘），1-7 = 按键（K1-K7）**
- shuffle/random 映射在 `JudgementEngine::compute_lane_mappings()` 中执行

## 开发待办

### 高优先级
- [ ] beatoraja 0.8.6 及以前版本的回放兼容（testfile 中需补充旧版 brd 样本）
- [ ] 回放解析解耦重构（工厂1: 格式分派 + 工厂2: brd 版本分派 + 统一 ReplayData，详见 ADR-22）
- [ ] 详细的判定分析和统计（FAST/SLOW 分布、mean/stddev 时序偏移可视化、逐 note 判定详情面板）

### 中优先级
- [ ] UI 大修（Controls Panel 重构已完成，剩余范围待定）+ BMS 注释语法支持 (`//`, `;`, `/* */`)
- [ ] 元数据显示：`#SUBTITLE`, `#SUBARTIST`, `#COMMENT`, `#DIFFICULTY`
- [ ] 字体重绘（添加完整字母支持，修复 BPM/STOP 标签显示不完整）
- [ ] 支持其他程序传参调用 + replay 目录内 hash 筛选
- [ ] `#BASE` 62 进制支持（修复 62 进制谱面 `#WAV/#BPM` 索引解析错误）
- [ ] `.bmson` 解析支持 (`BmsonParser`)
- [ ] 5K / 9L / 14K BMS 布局适配（当前仅支持 7K）
- [ ] FFmpeg 管道视频导出（功能已完成，文档同步）
- [ ] SHA256/MD5 哈希校验（功能已完成，文档同步）

### 低优先级
- [ ] 谱面波形图
- [ ] `#LNTYPE 1` 通道支持 (0x51-0x69)
- [ ] 地雷通道 (D1-D9, E1-E9) 可视化

## Early testers

Steve58313、LED、Yuntian52、Reiaki秋

Thank you all!
