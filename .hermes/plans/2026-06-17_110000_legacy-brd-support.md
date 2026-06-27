# 旧版本 Beatoraja 回放格式支持 Implementation Plan

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task.

**Goal:** 让 BRD 回放解析器同时支持旧版本（keylog 数组）和新版本（keyinput 编码）格式

**Architecture:** 在 JSON 解析后检测字段类型，根据 `keyinput` 或 `keylog` 字段选择不同的解析流程。新增 `brd_decode_keylog` 函数处理旧版格式的字典数组。

**Tech Stack:** C++17, nlohmann/json, 现有 gzip/base64 工具链

---

## 背景

### 新旧格式对比

**新版本格式** (当前已支持):
```
文件 → GZIP 解压 → JSON → keyinput (base64 string) → base64 解码 → GZIP 解压 → 9字节帧
```

JSON 结构:
```json
{
  "player": "...",
  "sha256": "...",
  "keyinput": "base64编码的字符串...",
  "gauge": 2,
  "laneShufflePattern": [[...]],
  ...
}
```

二进制帧格式 (每帧 9 字节):
- byte[0]: int8_t keycode (正数=按下, 负数=释放, abs-1=实际keycode)
- byte[1-8]: int64_t timestamp (微秒, little-endian)

**旧版本格式** (需要支持):
```
文件 → GZIP 解压 → JSON → keylog (数组) → 直接解析字典数组
```

JSON 结构:
```json
{
  "player": "...",
  "sha256": "...",
  "keylog": [
    {"presstime": 1215428, "keycode": 3, "pressed": true},
    {"presstime": 1639889, "keycode": 6},
    ...
  ],
  "gauge": 2,
  ...
}
```

字典字段:
- `presstime`: int64 微秒时间戳
- `keycode`: int 0-7 (物理按键)
- `pressed`: bool (可选，true=按下，缺失或false=释放)

### 关键差异

1. **字段名**: `keyinput` (新) vs `keylog` (旧)
2. **数据类型**: string (新) vs array (旧)
3. **编码**: base64+GZIP (新) vs 无编码 (旧)
4. **keycode 含义**: 
   - 新版: 物理按键 1-8，需要转换 (keycode-1 → 0-7，7=scratch)
   - 旧版: 直接 0-7 (0-6=keys, 7=scratch)
5. **lane 映射**:
   - 新版: keycode 7→lane 0 (scratch), 0-6→lane 1-7
   - 旧版: keycode 7→lane 0 (scratch), 0-6→lane 1-7 (相同)

---

## 实施计划

### Task 1: 添加版本检测逻辑

**Objective:** 在 JSON 解析后检测字段类型，确定使用哪种解析流程

**Files:**
- Modify: `src/replay/brd_parser.cpp:34-62` (brd_unwrap_json 函数后)

**Step 1: 添加版本检测辅助函数**

在 `brd_unwrap_json` 函数之后添加:

```cpp
// 检测 BRD 格式版本
// 返回: "new" (keyinput), "old" (keylog), "unknown"
static const char* brd_detect_version(const nlohmann::json& j) {
    if (j.contains("keyinput") && j["keyinput"].is_string()) {
        return "new";
    }
    if (j.contains("keylog") && j["keylog"].is_array()) {
        return "old";
    }
    return "unknown";
}
```

**Step 2: 编译验证**

Run: `cmake --build build --config Release`
Expected: 编译成功，无错误

**Step 3: Commit**

```bash
git add src/replay/brd_parser.cpp
git commit -m "feat(brd): add version detection for legacy format support"
```

---

### Task 2: 实现旧版 keylog 解析函数

**Objective:** 创建 `brd_decode_keylog` 函数，解析旧版格式的字典数组为 RawInputEvent

**Files:**
- Modify: `src/replay/brd_parser.cpp:104` (brd_decode_frames 函数后)

**Step 1: 添加 keylog 解析函数**

在 `brd_decode_frames` 函数之后添加:

```cpp
// Parse keylog array from legacy BRD format.
// Each element: {"presstime": int64, "keycode": int 0-7, "pressed": bool (optional)}
// keycode: 0-6 = keys (lane 1-7), 7 = scratch (lane 0)
static std::vector<RawInputEvent> brd_decode_keylog(const nlohmann::json& keylog,
                                                      int64_t& out_duration_us) {
    std::vector<RawInputEvent> events;
    
    for (const auto& entry : keylog) {
        if (!entry.is_object()) continue;
        
        // 提取 presstime (必需)
        if (!entry.contains("presstime") || !entry["presstime"].is_number_integer()) {
            continue;
        }
        int64_t ts = entry["presstime"].get<int64_t>();
        
        // 提取 keycode (必需)
        if (!entry.contains("keycode") || !entry["keycode"].is_number_integer()) {
            continue;
        }
        int keycode = entry["keycode"].get<int>();
        if (keycode < 0 || keycode > 7) continue;
        
        // 提取 pressed (可选，默认 false = 释放)
        bool pressed = false;
        if (entry.contains("pressed") && entry["pressed"].is_boolean()) {
            pressed = entry["pressed"].get<bool>();
        }
        
        // keycode → lane 映射 (与新版相同)
        uint8_t lane;
        if (keycode == 7)      lane = 0;         // Scratch → lane 0
        else /* keycode 0-6 */ lane = static_cast<uint8_t>(keycode + 1); // K0-K6 → lane 1-7
        
        events.push_back({ts, lane, pressed});
    }
    
    if (!events.empty()) out_duration_us = events.back().time_us;
    return events;
}
```

**Step 2: 编译验证**

Run: `cmake --build build --config Release`
Expected: 编译成功，无错误

**Step 3: Commit**

```bash
git add src/replay/brd_parser.cpp
git commit -m "feat(brd): implement keylog parser for legacy format"
```

---

### Task 3: 修改主解析流程支持双版本

**Objective:** 在 `parse_replay_input` 中根据版本检测结果选择正确的解析路径

**Files:**
- Modify: `src/replay/brd_parser.cpp:127-163` (parse_replay_input 函数)

**Step 1: 修改 parse_replay_input 函数**

替换 `parse_replay_input` 函数为:

```cpp
ReplayInput BrdParser::parse_replay_input(const std::string& filepath) {
    ReplayInput input;
    input.format = ReplayFormat::BRD;

    auto raw = brd_read_file(filepath);
    if (raw.empty()) {
        return input;
    }

    nlohmann::json j;
    auto json_bytes = brd_unwrap_json(raw, j);
    if (json_bytes.empty()) return input;

    input.brd = brd_extract_meta(j);

    // 检测 BRD 版本并选择解析流程
    const char* version = brd_detect_version(j);
    
    if (std::strcmp(version, "new") == 0) {
        // 新版本: keyinput (base64 + GZIP)
        std::string keyinput = j["keyinput"].get<std::string>();
        
        auto decoded = base64::decode(keyinput);
        if (decoded.empty()) {
#ifdef BMV_DEBUG
            std::fprintf(stderr, "[BrdParser] base64 decode produced no data\n");
#endif
            return input;
        }

        auto bin = gzip_decompress(decoded.data(), decoded.size());
        if (bin.empty()) return input;

        input.events = brd_decode_frames(bin, j, input.duration_us);
        
    } else if (std::strcmp(version, "old") == 0) {
        // 旧版本: keylog (JSON 数组)
        const auto& keylog = j["keylog"];
        input.events = brd_decode_keylog(keylog, input.duration_us);
        
    } else {
        // 未知版本
#ifdef BMV_DEBUG
        std::fprintf(stderr, "[BrdParser] unknown BRD format: missing keyinput or keylog\n");
#endif
        return input;
    }
    
    return input;
}
```

**Step 2: 编译验证**

Run: `cmake --build build --config Release`
Expected: 编译成功，无错误

**Step 3: Commit**

```bash
git add src/replay/brd_parser.cpp
git commit -m "feat(brd): support both legacy and new BRD formats"
```

---

### Task 4: 测试旧版回放文件解析

**Objective:** 验证旧版 BRD 文件能够正确解析

**Files:**
- Test: `testfiles/086/086_normal_bc1462814a47c6a0e2d6a6779014f78e8f6f94c3b49ab6ecb9a5752d18137e16.brd`

**Step 1: 运行 BMV 加载旧版回放**

Run: `build/Release/bmv.exe`
操作:
1. 拖拽 `testfiles/086/anata_g24.bme` 到窗口
2. 拖拽 `testfiles/086/086_normal_*.brd` 到窗口
3. 检查是否正确显示回放数据

Expected:
- 回放成功加载
- 显示判定统计 (PG/GR/GD/BD/POOR)
- 回放事件正常叠加显示

**Step 2: 验证随机模式回放**

Run: 测试 `086_random_*.brd` 文件

Expected:
- 随机模式回放正常加载
- lane 映射正确 (shuffle pattern 应用)

**Step 3: 验证新版本回放仍然正常**

Run: 加载 `testfiles/sp/*.brd` (新版本格式)

Expected:
- 新版本回放仍然正常工作
- 无回归问题

**Step 4: 编译并运行测试 (如果有)**

Run: `cmake --build build --config Release && ctest --test-dir build --config Release`
Expected: 所有测试通过

**Step 5: Commit (如果有修复)**

```bash
git add .
git commit -m "test(brd): verify legacy format parsing"
```

---

### Task 5: 清理代码和文档

**Objective:** 移除 TODO 注释，更新文档说明

**Files:**
- Modify: `src/replay/brd_parser.cpp:48-58` (移除 TODO 注释)
- Modify: `readme.md` (可选，添加格式支持说明)

**Step 1: 移除 TODO 注释**

删除 `brd_unwrap_json` 函数中的 TODO 注释块 (第 48-58 行):

```cpp
// TODO: Phase X - BRD version compatibility
// [位置保留] 旧版本 BRD 格式检测与分支逻辑
// 根据 JSON 结构/版本号判断，调用不同的解析流程
// 当前: 仅支持最新版本
// Version detection stub:
// int brd_version = detect_brd_version(j);
// if (brd_version < 0) {
// #ifdef BMV_DEBUG
//     std::fprintf(stderr, "[BrdParser] Unknown BRD version, attempting latest format...\n");
// #endif
// }
```

**Step 2: 更新 readme.md (可选)**

在 readme.md 的功能列表中添加:
```
- 支持新旧版本 Beatoraja 回放格式 (keyinput 和 keylog)
```

**Step 3: 编译验证**

Run: `cmake --build build --config Release`
Expected: 编译成功

**Step 4: Commit**

```bash
git add src/replay/brd_parser.cpp readme.md
git commit -m "docs(brd): remove TODO and update format support documentation"
```

---

## 验证清单

- [ ] 旧版 normal 模式回放正常加载
- [ ] 旧版 random 模式回放正常加载
- [ ] 新版回放仍然正常工作 (无回归)
- [ ] 编译无警告
- [ ] 代码符合项目风格
- [ ] TODO 注释已清理

---

## 风险与注意事项

1. **keycode 映射一致性**: 确保旧版和新版的 keycode → lane 映射逻辑一致
   - 新版: keycode 1-8 (物理), abs-1 后 0-7, 7=scratch→lane 0
   - 旧版: keycode 0-7 (直接), 7=scratch→lane 0
   - 两者最终映射结果应该相同

2. **pressed 字段缺失处理**: 旧版格式中 `pressed` 字段可选
   - 缺失时默认为 false (释放)
   - 需要验证这是否符合实际数据

3. **时间戳单位**: 确认旧版 `presstime` 单位是微秒 (与新版本一致)

4. **性能**: keylog 数组可能很大 (测试文件 4512 个元素)
   - JSON 数组遍历性能应该足够
   - 无需优化

5. **向后兼容**: 确保新版本格式解析不受影响
   - 版本检测逻辑必须准确
   - 优先检测 keyinput (新格式)

---

## 测试文件

- 旧版 normal: `testfiles/086/086_normal_bc1462814a47c6a0e2d6a6779014f78e8f6f94c3b49ab6ecb9a5752d18137e16.brd`
- 旧版 random 1: `testfiles/086/086_random_bc1462814a47c6a0e2d6a6779014f78e8f6f94c3b49ab6ecb9a5752d18137e16_1.brd`
- 旧版 random 2: `testfiles/086/086_randombc1462814a47c6a0e2d6a6779014f78e8f6f94c3b49ab6ecb9a5752d18137e16_2.brd`
- 谱面文件: `testfiles/086/anata_g24.bme`
- 新版对照: `testfiles/sp/*.brd`
