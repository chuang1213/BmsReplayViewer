## 简介

BMV (BMS Visualizer) 是一款 BMS 节奏游戏回放分析工具。导入谱面(.bms/.bme/.bml)和回放文件(.brd/.lr2rep)后，可以逐帧复盘演奏、对比判定差异、导出分析视频。

核心功能：
- 实时 GPU 渲染谱面视图 (ImGui + OpenGL)
- 回放按键叠加显示，支持 Line / Box / Marker 三种模式
- LR2 与 beatoraja 双判定系统，支持判定统计
- FAST/SLOW 标注与 mean/stddev 时序偏移（待进一步开发）
- 图片和视频导出
- 拖放加载 (自动识别谱面/回放)
- SHA256/MD5 哈希校验

（这是我的第一个开源项目，有很多不到位的地方还请多指教。）

## 现有缺陷
- .bmson 格式不知道支不支持
- 不支持 BMS 注释语法（//、;、/* */），含注释的谱面可能解析失败
- Channel 03 HEX BPM 丢弃了负值，不支持逆向滚动谱面
- #BASE 62 进制未支持，使用 62 进制的谱面 #WAV/#BPM 索引会解析错误
- 仅支持7keys
- PNG 静态渲染使用内嵌像素字体，BPM/STOP 标签可能显示不完整（GUI 模式元数据栏已使用系统 CJK 字体）

## 构建与使用

### 构建

```powershell
# 配置 (首次自动下载 GLFW + ImGui, 约 30s)
cmake -B build -G "Visual Studio 17 2022" -A x64

# Release 构建（GUI 子系统，双击不弹黑窗）
cmake --build build --config Release

# Debug 构建（控制台子系统，printf 可见）
cmake --build build --config Debug
```

### 运行

```powershell

# GUI 模式 + 启动时自动加载谱面
.\build\Release\bmv.exe --gui [bmx]

> 注：支持日文/中文路径。Windows 上 CLI 参数会自动转为 UTF-8，GUI 文件对话框和拖放均正确处理非 ASCII 路径。

## 开发待办

### 高优先级
- [x] beatoraja 0.8.6 及以前版本的回放兼容（testfile 中需补充旧版 brd 样本）
- [x] Random 模块解耦（将 lane shuffle 算法从 replay 解析中分离，独立为 random/ 模块）
- [x] 回放解析解耦重构（工厂1: 格式分派 + 工厂2: brd 版本分派 + 统一 ReplayData，详见 ADR-22）
  - [x] 阶段1: 类型定义与随机算法纯函数
  - [x] 阶段2: BRD 解析器（新旧版本）
  - [x] 阶段3: LR2REP 解析器
  - [x] 阶段4: 工厂函数 `parse_replay`
  - [x] 阶段5: 适配层 `unified_to_replay_data`
    - [x] 阶段6: 集成测试
- [x] 详细的判定分析和统计（FAST/SLOW 分布、mean/stddev 时序偏移可视化、逐 note 判定详情面板）

### 中优先级
- [x] UI 大修
- [x] BMS 注释语法支持 (`//`, `;`, `/* */`)
- [x] 元数据显示：`#SUBTITLE`, `#SUBARTIST`, `#COMMENT`, `#DIFFICULTY`
- [x] 字体支持（GUI 元数据栏使用系统 CJK 字体显示日文 Title/Artist）
- [x] 支持其他程序传参调用（`--gui <file>` 模式）+ replay 目录内 hash 筛选
- [ ] `#BASE` 62 进制支持
- [ ] `.bmson` 解析支持 
- [ ] 5K / 9L / 14K BMS 布局适配（当前仅支持 7K）
- [x] FFmpeg 管道视频导出
- [x] SHA256/MD5 哈希校验
- [x] 跨平台路径编码（UTF-8 路径统一处理，支持日文/中文路径）

### 低优先级
- [x] 谱面波形图
- [ ] `#LNTYPE 1` 通道支持 (0x51-0x69)
- [ ] 地雷通道 (D1-D9, E1-E9) 可视化

## Early testers

Steve58313、LED、Yuntian52、Reiaki秋

Thank you all!
