# BMV 领域模型

## Replay 解析层

### 核心概念

**ReplayEvent**
- 单个按键事件
- 字段：`time_us`（微秒时间戳）、`lane`（0=scratch, 1-7=keys）

**UnifiedReplay**
- 统一格式，所有回放格式最终转换为此结构
- 字段：
  - `press_events`: 所有按下事件列表
  - `release_events`: 所有松开事件列表
  - `metadata`: 元数据

**ReplayMetadata**
- 回放元数据
- 字段：
  - `shuffle_pattern[8]`: 默认恒等映射 `{0,1,2,3,4,5,6,7}`
  - `random_option`: `std::optional<RandomMode>`，可能为空
  - `judgements`: `std::optional<std::vector<uint8_t>>`，LR2 的 op210 判定记录
  - `seed`: `std::optional<int>`，旧 BRD 和 LR2REP 有，用于调试
  - `duration_us`: 回放时长（微秒）

**RandomAlgorithm**
- 纯函数库
- 输入：seed + 类型（旧BRD / LR2）
- 输出：shuffle_pattern[8]
- 职责：根据种子和随机模式计算轨道排列

### 边界

- 解析器只负责从文件提取事件，不依赖 TimeMap
- 时间单位统一为 time_us（微秒），保持原始精度
- 下游（绘制、分析）自己负责 time_us → tick 的转换
- 随机算法是纯函数库，解析器调用它来填充 metadata.shuffle_pattern

### 格式差异

**BRD (beatoraja)**
- 新版：JSON 里直接有 `laneShufflePattern`
- 旧版：有 seed，需要用 Java 随机算法从 seed 算出 pattern
- keycode 编码：7=scratch, 0-6=keys → 映射到 lane 0=scratch, 1-7=keys

**LR2REP (LR2)**
- 有 seed，需要用 LR2 随机算法从 seed 算出 pattern
- op 编码：0=scratch, 1-7=keys → 直接就是 lane 0=scratch, 1-7=keys
- 有 op210 判定记录（逐 note 的判定结果）

### 职责划分

**解析器 (ReplayParser)**
- 输入：文件路径
- 输出：`std::optional<UnifiedReplay>`
- 职责：读取文件、解码/解压、提取事件、填充元数据
- 不依赖 TimeMap，不知道 BMS 的存在

**随机算法 (RandomAlgorithm)**
- 输入：seed + 类型
- 输出：shuffle_pattern[8]
- 职责：纯计算，无副作用

**下游消费者**
- 绘制：需要 tick → 调用 `time_map.second_to_tick(time_us / 1e6)`
- 判定分析：需要 time_us → 直接用
- 视频导出：需要 time_us → 直接用
