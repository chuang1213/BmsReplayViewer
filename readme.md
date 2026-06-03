## 简介

BMV (BMS Visualizer) 是一款 BMS 节奏游戏回放分析工具。导入谱面(.bms/.bme/.bml)和回放文件(.brd/.lr2rep)后，可以逐帧复盘演奏、对比判定差异、导出分析视频。

核心功能：
- 实时 GPU 渲染谱面视图 (ImGui + OpenGL)
- 回放按键叠加显示，支持 Line / Box / Marker 三种模式（没做完）
- LR2 与 beatoraja 双判定系统，支持 PGREAT/GREAT/GOOD/BAD/POOR 统计（没做完）
- FAST/SLOW 标注与 mean/stddev 时序偏移（要进一步）
- 静态 PNG 导出 (CLI)
- FFmpeg 管道视频导出 (没做完)
- 拖放加载 (自动识别谱面/回放)
- LR2 RANDOM/MIRROR 模式精确复刻
- SHA256/MD5 哈希校验 (没做完)

（这是我的第一个开源项目，有很多不到位的地方还请多指教。）

## 现有缺陷

- beatoraja 判定系统需要重构（当前窗口值不准确，且不支持 #RANK 4(VERY EASY)）
- 旧版本 oraja 回放不支持（仅支持 0.8.7+）
- .bmson 格式不知道支不支持
- 不支持 BMS 注释语法（//、;、/* */），含注释的谱面可能解析失败
- Channel 03 HEX BPM 丢弃了负值，不支持逆向滚动谱面
- #BASE 62 进制未支持，使用 62 进制的谱面 #WAV/#BPM 索引会解析错误
- 仅支持7k
- 内嵌像素字体缺少字母，BPM/STOP 标签无法显示完整
- 分开拖谱和回放有点麻烦

## Todo list

### 近期 (v0.4)
- [ ] beatoraja 判定系统重构
- [ ] BMS 注释语法支持 (//, ;, /* */)
- [ ] 元数据显示：#SUBTITLE, #SUBARTIST, #COMMENT, #DIFFICULTY
- [ ] 字体重绘 (添加字母支持)
- [ ] 支持其他程序传参调用 + replay目录内 hash 筛选 (简化拖谱流程)

### 中期 (v0.5)
- [ ] #BASE 62 进制支持
- [ ] .bmson 解析支持 (BmsonParser)
- [ ] 5K, 9L, 14K BMS 布局适配
- [ ] 回放解析解耦重构
- [ ] 谱面波形图

### 远期 (v1.0)
- [ ] #LNTYPE 1 通道支持 (0x51-0x69)
- [ ] 地雷通道 (D1-D9, E1-E9) 可视化

## Early testers

Steve58313、LED、Yuntian52、Reiaki秋

Thank you all!
