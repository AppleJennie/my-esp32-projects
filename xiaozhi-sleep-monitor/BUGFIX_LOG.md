# 睡眠监测系统 Bug 修复日志
## 日期: 2026-07-04
## 原则: 最小改动, 稳定优先, 不破坏现有数据链

---

### Bug #1: UpdateBreathWave 调 RecordBreathEvent 递归死锁 🔴

**位置**: `sleep_data_center.cpp`
**严重度**: 致命 — 第一次 ≥10s 呼吸事件结束时任务永久卡死

**原因**: 
- `UpdateBreathWave()` 入口处调用 `Lock()` (FreeRTOS mutex, 非递归)
- 事件结束时调用 `RecordBreathEvent()`, 该函数内部又调 `Lock()`
- 同一任务对同一非递归 mutex 两次 Lock → 死锁

**修复**:
- `RecordBreathEvent()` 去掉内部 `Lock()/Unlock()` 
- 添加注释: "调用者必须已持有 mutex"
- `UpdateBreathWave()` 持有锁后调用, 不会死锁

**影响文件**: `sleep_data_center.cpp`

---

### Bug #2: 融合事件按秒重复计数 🔴

**位置**: `sleep_fusion.c`
**严重度**: 严重 — 一次 20 秒暂停被计数 20 次

**原因**:
- `s_apnea_total++` 和 `s_hypopnea_total++` 在检测分支内
- 每秒 tick 时条件满足就 +1, 未做首次锁存

**修复**:
- 每次增量前检查 `s_state != 目标事件类型`, 只在首次进入时计数
- `s_apnea_total++` → `if (s_state != FUSION_OBSTRUCTIVE_LIKE) s_apnea_total++`
- `s_apnea_total++` → `if (s_state != FUSION_CENTRAL_LIKE) s_apnea_total++`
- `s_hypopnea_total++` → `if (s_state != FUSION_HYPOPNEA_LIKE) s_hypopnea_total++`

**影响文件**: `sleep_fusion.c`

---

### Bug #3: 呼吸幅度用全窗口 max-min 导致短时暂停检测迟钝 🟡

**位置**: `r60abd1_adapter.c`
**严重度**: 中等 — 10 秒呼吸暂停不会立刻反映在 breath_amp 上

**原因**:
- R60 手册: DP10 呼吸波形 5Hz, 300 点 = 60 秒
- 旧代码遍历全部 300 点取 max-min, 且从头遍历(最旧数据)
- 发生 10 秒暂停时, 前 50 秒正常幅度的 max/min 主导结果

**修复**:
- 改为从 ring buffer 尾部取最近 50 点(10 秒 @5Hz)
- 使用 `breath_wave_idx` 反向索引, 取最新数据
- 心率波形同样改为 50 点短窗口

**影响文件**: `r60abd1_adapter.c`

---

---

### Bug #4: Composite 8字节解析全错位 🔴

**位置**: `sleep_radar_data.c` → `sleep_radar_data_update_sleep_composite()`
**严重度**: 致命 — 8个字段全部映射错位

**原因**:
- 旧代码按猜测的顺序映射字段
- 对比 R60ABD1 手册 V3.0, 正确顺序为:
  [0]大运动占比 [1]小运动占比 [2]呼吸暂停 [3]睡眠状态
  [4]平均呼吸 [5]平均心跳 [6]翻身次数 [7]存在状态
- 旧代码把 payload[0] 当存在状态, payload[7] 当呼吸暂停

**修复**:
- 全部 8 个字段按手册重新映射

**影响文件**: `sleep_radar_data.c`

---

### Bug #5: 未发送睡眠模式切换命令 🔴

**位置**: `r60abd1_uart.c` → `r60abd1_send_init_commands()`
**严重度**: 严重 — 雷达可能在实时模式, 缺少 DP11~DP20 睡眠功能

**原因**:
- 手册规定必须下发 `0x84 0x0F 0x01` 切到睡眠模式
- 旧 init 只发了 presence/breath/heart 开关, 没发模式切换

**修复**:
- init 命令新增 R60_INIT_SLEEP_MODE: `53 59 84 0F 00 01 01 E0 54 43`
- 头文件新增宏定义

**影响文件**: `r60abd1_uart.c`, `r60abd1_uart.h`

---

### 编译命令

```
idf.py -DCMAKE_CXX_USE_RESPONSE_FILE_FOR_INCLUDES=ON -DCMAKE_C_USE_RESPONSE_FILE_FOR_INCLUDES=ON build
```

### 回滚方法

每个 bug 修改都标注了位置和原逻辑, 可通过 git diff 或 IDE history 回滚。
