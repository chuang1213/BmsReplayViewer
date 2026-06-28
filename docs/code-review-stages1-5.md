# 代码审查报告：replay 解析层重构（阶段1-5）

审查日期：2026-06-28
审查范围：src/replay/unified/ 全部新增代码 + 旧代码修改

## 一、总体评价

整体架构清晰，新旧代码完全隔离，unified/ 子目录不碰旧代码，适配层通过测试验证与旧路径输出一致。没有破坏性改动。

## 二、确认无破坏性

- 旧代码（replay_factory.cpp, replay_adapter.cpp, brd_parser.cpp, lr2rep_parser.cpp）没有被修改，bmv.exe 仍走旧路径
- 新代码全部在 src/replay/unified/ 下，独立编译为测试可执行文件
- Application::load_replay_file 仍调用旧的 parse_replay + replay_input_to_replay_data，新代码没有接入主程序
- 阶段1修复的坐标转换 bug 改的是旧代码（judgement_engine.cpp, png_renderer.cpp, brd_parser.cpp），但这些修复已在 dev/uirebuild 上验证过，bmv.exe 能正常编译运行

## 三、问题与改进建议

### 问题1：新旧 random 模块有重复代码
unified/random.cpp 调用旧的 java_random.cpp 和 lr2_random.cpp，相当于包了一层。旧代码里 build_brd_random_pattern 和 build_random_lane_pattern 仍然是实际算法所在。

状态：可接受。适配层设计就是这样过渡的。未来 Application 层重构时，可以把算法直接搬到 unified/random.cpp，删除旧文件。

### 问题2：adapter 中 LR2 的 random_mode 只填了 P1 槽位
adapter.cpp 第117-119行只填充了 has_random_info[0] 和 random_mode[0]，has_random_info[1] 和 random_mode[1] 始终是默认值。

旧代码同样只填 P1（replay_adapter.cpp 第85-88行），所以行为一致，但 UnifiedReplay 的 metadata 里没有 P2 的概念——LR2 解析器虽然提取了 p2_random 但在 metadata.random_option 里只保留了一个（P1 优先）。

影响：如果将来需要区分 P1/P2，需要改 UnifiedReplay 的 metadata 结构。当前 7K 单人模式下无影响。

### 问题3：lr2_parser 中 value != 0 且 value != 1 的事件被丢弃
第86行 down = (value == 1)，其他值直接走 release 分支。实际上代码是：value==1 → press_events，其他所有值（包括0）→ release_events。这和旧 lr2rep_parser.cpp 的行为一致。

状态：正确，无问题。

### 问题4：parser.cpp 的 BRD 路径没有处理 GZIP 解压返回部分数据的情况
gzip::decompress 返回空 vector 表示失败，但如果解压出部分损坏数据，parser 会把损坏数据当 JSON 解析，json::parse 用了 false 参数（不抛异常），所以会走到 json_parse_failed 错误。

状态：可接受，错误路径覆盖了。

### 问题5：adapter 中 BRD 的排序策略
adapter.cpp 第56-60行用 stable_sort，press 优先于 release。这和旧 replay_adapter.cpp 的行为不同——旧代码是按文件原始顺序处理的，不做排序。

影响：如果 BRD 文件里事件本身就是按时间排序的（实际上是的），那么 stable_sort 不会改变顺序，行为一致。测试也验证了 hits 数量一致。但如果存在乱序事件，新旧路径可能产生不同结果。

风险：低。BRD 格式的事件天然按时间排序。

### 问题6：test_adapter 的等价性验证不够严格
测试对比新旧 adapter 时，对 hits 做了 sort 后再逐项比较（sort_hits）。这意味着如果新旧路径产生相同集合但不同顺序的 hits，测试也会通过。

影响：对于阶段5（适配层验证）来说够了，但阶段6集成测试应该验证顺序一致性。

### 问题7：has_shuffle 的语义差异
旧 BrdMeta.has_shuffle 表示 JSON 里有没有 laneShufflePattern 字段。新 adapter 用 !pattern_is_identity() 来推断 has_shuffle，即 shuffle_pattern 不是恒等映射就算 has_shuffle=true。

差异场景：如果 BRD 文件里有 laneShufflePattern 且恰好是 {0,1,2,3,4,5,6,7}（恒等），旧代码 has_shuffle=true，新代码 has_shuffle=false。

影响：几乎没有，恒等的 shuffle_pattern 等于没有 shuffle。但下游如果依赖 has_shuffle 做分支，可能出现行为差异。

## 四、接入主程序的风险点（阶段6需要注意）

application.cpp 第116-121行是接入点：
```
旧：auto input = parse_replay(path);
    replay_data_ = replay_input_to_replay_data(input.value(), ...);
新：auto ur = parse_replay(path, &err);
    replay_data_ = unified_to_replay_data(*ur, tm, fmt);
```

接入时需要注意：
1. 确定 format 的方式变了——旧路径从 ReplayInput.format 获取，新路径需要从文件扩展名推断
2. hash 校验逻辑（第129-143行）依赖 input->format，新路径需要用 ReplayFormat 变量替代
3. 错误处理——旧路径 parse_replay 返回 nullopt 时只打一行 stderr，新路径可以拿到结构化 ParseError，可以给用户更好的错误信息

## 五、结论

阶段1-5 代码质量良好，没有破坏性问题。新旧路径完全隔离，可以安全地继续阶段6接入。主要关注点是接入时的 format 判断和 hash 校验逻辑。