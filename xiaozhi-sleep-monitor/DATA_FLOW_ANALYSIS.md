# 睡眠监测系统 — 完整数据链路分析

## 一、硬件总线分配

| 总线 | 设备 | 引脚 | 状态 |
|---|---|---|---|
| UART1 | R60 毫米波雷达 | RX=17, TX=18 | ⚠️ 雷达间歇停发 |
| UART2 | UI 板双向通讯 | RX=19(收SNORE), TX=20(发MAIN) | ✅ |
| I2C0 | ES8388 音频 + AP3216C 光照 | SDA=41, SCL=42 | ✅ |
| I2C1 | SHT30 温湿度 | SDA=4, SCL=16 | ✅ |
| SPI2 | LCD ST7735 | SCK=5, MOSI=45, CS=15, DC=47 | ✅ |
| SPI3 | MicroSD 卡 | CS=2, SCK=12, MOSI=11, MISO=13 | ✅ |
| I2S | ES8388 语音 | MCLK=3, WS=9, BCLK=46, DIN=14, DOUT=10 | ✅ |

## 二、每条数据链路的完整追踪

### 链路1：R60雷达 → 心率/呼吸率 → MAIN报告

```
R60 毫米波雷达
  │  UART1 TX → ESP32 GPIO17 (UART1 RX)
  ▼
r60abd1_uart.c: r60abd1_uart_task() [轮询 50ms]
  │  uart_read_bytes → ring_push → try_extract_frame()
  │  帧头53 59 → 校验和 → 帧尾54 43
  ▼
r60abd1_uart.c: dispatch_frame(ctrl, cmd, len, payload, now)
  │  ctrl=0x85 cmd=0x02 → sleep_radar_data_update_heart_rate(val, now)
  │  ctrl=0x81 cmd=0x02 → sleep_radar_data_update_breath_rate(val, now)
  │  ctrl=0x80 cmd=0x03 → sleep_radar_data_update_body_motion(val, now)
  ▼
sleep_radar_data.c: sleep_radar_data_t (全局, mutex保护)
  │  heart_rate, breath_rate, body_motion
  │  last_heart_rate_ms, last_breath_rate_ms, last_body_motion_ms
  │  ⚠️ radar_connected 仅由 heartbeat(ctrl=0x01) 设置
  ▼
fusion_task.cc: fusion_tick_task() [每秒]
  │  sleep_radar_data_get_snapshot(&radar_snapshot)
  │  r60abd1_adapter_convert(&radar_snapshot, &rf) → radar_feature_t
  │  sleep_fusion_feed_radar(&rf)
  │  SleepDataCenter::UpdateRadar(presence, motion, hr, br, dist)
  ▼
main_report_uart.cpp: main_report_uart_send() [每秒]
  │  sleep_radar_data_get_snapshot(&radar_snapshot)
  │  get_radar_status(&radar_snapshot)
  │    ⚠️ 检查 radar_connected → 如果雷达不发心跳帧, 永远返回0
  │  heart_valid = freshness(last_heart_rate_ms) ≤ 5000ms
  │  breath_valid = freshness(last_breath_rate_ms) ≤ 5000ms
  │  snprintf("MAIN,%lu,%d,%d,%d,%d,%d,...", ..., radar_status, heart_valid, heart_bpm, ...)
  ▼
uart_write_bytes(UART2, buf) → GPIO20 TX → UI板 GPIO19 RX
```

**🔴 问题**: `get_radar_status()` 依赖 `radar_connected` 标志。该标志仅在收到心跳帧(ctrl=0x01 cmd=0x01)时被设为true。如果雷达固件不发心跳帧, `radar_connected` 永远为 false, MAIN 报告永远显示 `radar=0`。

**修复(已提交,待编译)**: try_extract_frame() 收到任何有效帧都设置 `radar_connected=true`。

---

### 链路2：UI板 → 鼾声数据 → 融合引擎

```
UI板 INMP441 麦克风 → 音频AI → TFLite Micro 推理
  │  ⚠️ UI板当前 mic=0 (INMP441未接或初始化失败)
  │  UART2 TX → ESP32 GPIO19 (UART2 RX)
  ▼
snore_report_rx.cpp: snore_rx_task()
  │  uart_read_bytes → 逐行解析
  │  "SNORE,ts,mic,audio,snore_active,score,dB,rms,peak,zcr,..."
  │  parse_snore_line() → snore_report_t
  │  接受 >=14 字段 (v2兼容)
  ▼
snore_report_rx.cpp: latest_store() [mutex保护]
  ▼
fusion_task.cc: fusion_tick_task() [每秒]
  │  snore_report_rx_get_latest(&snore) → 新鲜度检查(≤3000ms)
  │  snore_to_audio_feature(&snore, &af) → audio_feature_t
  │  sleep_fusion_feed_audio(&af)
  │  SleepDataCenter::UpdateSnore(prob, is_snore, window_sec)
  │  SleepDataCenter::UpdateAudioSummary(rms, vad)
  ▼
sleep_fusion.c: sleep_fusion_tick()
  │  120s ring buffer → window_features(10s/30s) → baseline比较
  │  → 体动优先抑制 → 事件判定 → fusion_result_t
```

**🔴 问题**: UI板 `mic=0 valid=0` — 不是主板代码问题,UI板INMP441硬件未接。

---

### 链路3：融合事件 → 评分 → MAIN报告(扩展字段)

```
sleep_fusion.c: sleep_fusion_tick()
  │  fusion_result_t { event, confidence, severity, evidence_flags, ... }
  ▼
main_report_uart.cpp: main_report_uart_send()
  │  sleep_fusion_get_result(&fr) → event_id = fusion_event_to_id(fr.event)
  │  event_confidence = fr.confidence
  │  sleep_fusion_get_stats(&apnea_cnt, &hypopnea_cnt, ...)
  ▼
MAIN 报告的 event_id, apnea_cnt, hypopnea_cnt, event_confidence

────────────────────────────────────────

SleepDataCenter 统计
  │  apnea_like_count_, hypopnea_like_count_ (来自 UpdateBreathWave)
  │  motion_count_, heart_rate_count_, breath_rate_count_
  │  spo2_below_90_count_, spo2_drop_3pct_count_
  ▼
fusion_task.cc:
  │  SleepDataCenter::UpdateCachedSnoreRespScore()
  │  → CalcSnoreRespRisk(SnoreRespFeatures) → SnoreRespResult.score
  │  → cached_snore_resp_score_
  ▼
main_report_uart.cpp:
  │  snore_resp_score = GetCachedSnoreRespScore()
  │  → MAIN 报告 resp_score 字段
```

**🔴 问题**: `UpdateCachedSnoreRespScore()` 在 session_running_==false 且无雷达数据时,
  CalcSnoreRespRisk 中 has_radar=false 应返回 score=0, 
  但 motion_count_/breath_rate_count_ 可能因为 UpdateRadar() 在无session时仍被调用而积累了数据,
  导致 has_radar=true, rrei=0.0, 最终 raw_score=86, 无血氧归一化后 score>100→clamp到100。

**修复(已提交,待编译)**: UpdateCachedSnoreRespScore() 开头检查 session_running_ 和 (motion_count_==0 && breath_rate_count_==0), 不满足则返回0。

---

### 链路4：环境传感器 → MAIN报告

```
SHT30 (I2C1, GPIO4/16)
  │  sht30_read() → temperature, humidity
  ▼
AP3216C (I2C0, GPIO41/42)
  │  ap3216c_read() → light_raw
  ▼
fusion_task.cc: read_onboard_sensors() [每5秒]
  │  UpdateEnvironment(temp, humi, light) → SleepDataCenter
  │  温度 < 18°C → "偏低"  湿度 < 40% → "偏低"  光照 > 500 → "偏高"
  ▼
main_report_uart.cpp: main_report_uart_send()
  │  sht30_read(&sht) → temp_valid=1, temp_x10=温度×10, hum_x10=湿度×10
  │  → MAIN 报告 temp_valid, temp_x10, hum_x10
```

**⚠️ 已修复**: read_onboard_sensors() 之前SHT30和AP3216C各调一次UpdateEnvironment导致温湿度被-999覆盖。
现在合并为一次调用: 先读SHT30→读AP3216C→统一调UpdateEnvironment(temp, humi, light)。

---

### 链路5：SPO2血氧 (代码就绪,无数据源)

```
snore_report_rx.cpp: 解析 "SPO2,98" 行
  │  UpdateSpo2(98.0f) → SleepDataCenter
  │  has_spo2_ = true
  │  spo2_sum_, spo2_count_, min_spo2_
  │  spo2_below_90_count_, spo2_below_90_sec_
  │  spo2_drop_3pct_count_ (指数平滑基线 + 下降≥3%检测)
  ▼
CalcSleepScore: min_spo2<90→-15, avg_spo2<95→-5
CalcSnoreRespRisk: 低氧负荷35分, T90>10%→归零, spo2<85→≥grade3
MAIN报告: spo2_valid, spo2
```

**⚠️ 等待从板发送 SPO2 行**。代码已就绪, `snore_report_rx.cpp` 解析 "SPO2,val\n" 格式。

---

### 链路6：BLE手表血氧 (已禁用)

```
watch_ble_receiver.cpp (NimBLE Central)
  │  NimBLE头文件 → bt组件 → 命令行长度超限 → 禁用
  │  在被 fusion_task.cc 中注释: /* watch_ble_receiver_init(); */
  │  在 CMakeLists.txt 中未被添加
  │  文件保留在仓库,处于休眠状态
```

**🔴 已禁用**: Windows CreateProcess 命令行32767字符限制,加 bt 组件后超限。
备选方案: 外置BLE透传模块(JDY-23)接空闲GPIO/UART。

---

### 链路7：SD卡日志

```
sleep_sd_logger.cpp (SPI3, CS=2 SCK=12 MOSI=11 MISO=13)
  │  sleep_sd_logger_open_session(date, time) → /sdcard/sleep/YYYY-MM-DD/session_HH-MM-SS/
  │  sleep_sd_logger_write_realtime(ts, hr, br, spo2, present, motion, temp, humi, light)
  │  sleep_sd_logger_write_snore(ts, duration, prob, rms)
  │  sleep_sd_logger_write_report_json(report_json)
  │  sleep_sd_logger_write_report_txt(report_txt)
  │  sleep_sd_logger_close_session()
```

**⚠️ 启动日志显示 writes=0**, StartSession/StopSession 未被调用,所以没写入。

---

### 链路8：MCP工具 → SleepDataCenter

```
atk_dnesp32s3.cc: InitializeTools()
  │  mcp.AddTool("self.sleep.start", ...)          → StartSession()
  │  mcp.AddTool("self.sleep.stop", ...)           → StopSession()
  │  mcp.AddTool("self.sleep.status", ...)         → GetStatusJson()
  │  mcp.AddTool("self.sleep.get_vitals", ...)     → GetVitalsJson()
  │  mcp.AddTool("self.sleep.get_snore_status", ...)→ GetSnoreStatusJson()
  │  mcp.AddTool("self.sleep.get_environment", ...)→ GetEnvironmentJson()
  │  mcp.AddTool("self.sleep.get_apnea_risk", ...) → GetApneaRiskJson()
  │  mcp.AddTool("self.sleep.get_night_summary", ...)→ GetNightSummaryJson()
  │  mcp.AddTool("self.sleep.get_sensor_quality", ...)→ GetSensorQualityJson()
  │  mcp.AddTool("self.sleep.explain_current_risk", ...)→ GetNightSummaryJson()
  │  mcp.AddTool("self.sleep.generate_report", ...) → GenerateReportJson()
```

**⚠️ start/stop**: 从 AddUserOnlyTool 改回 AddTool, AI可直接调用。

---

## 三、已知问题汇总

| # | 问题 | 位置 | 严重度 | 状态 |
|---|---|---|---|---|
| 1 | radar_connected仅由心跳帧设置 | r60abd1_uart.c, get_radar_status | 🔴 | 已修复待编译 |
| 2 | resp_score=100（未监测时） | UpdateCachedSnoreRespScore | 🔴 | 已修复待编译 |
| 3 | 雷达间歇停发 | R60硬件 | 🔴 | 硬件排查 |
| 4 | UI板 mic=0 | UI板 INMP441 | 🟡 | UI板硬件 |
| 5 | BLE 手表代码编译不过 | watch_ble_receiver.cpp | 🟡 | 已禁用 |
| 6 | SPO2 无数据源 | 从板不发SPO2行 | 🟡 | 代码就绪 |
| 7 | wifi_ok/sd_ok 写死为2(未知) | main_report_uart.cpp | 🔵 | TODO |
| 8 | sleep_stage 写死为0 | main_report_uart.cpp | 🔵 | 雷达数据已接,MAIN未发 |
| 9 | SDNN/breath_cv 代理未计算 | CalcSleepScore | 🟡 | 部分实现 |
| 10 | duplicate UpdateWaveFeatures | sleep_data_center.cpp | 🔴 | 上次修了但可能残留 |

## 四、当前各文件状态

| 文件 | 状态 | 说明 |
|---|---|---|
| r60abd1_uart.c | ✅ 已修改 | 加radar_connected设置, ctrl=0x84全子命令, composite查询 |
| r60abd1_uart.h | ✅ 已修改 | 加R60_QUERY_SLEEP_COMPOSITE |
| sleep_radar_data.h | ✅ 已修改 | 波形缓冲60→300 |
| sleep_fusion.c | ✅ 已修改 | evidence_flags, 事件命名去医学化 |
| fusion_types.h | ✅ 已修改 | evidence_flags宏, FUSION_OBSTRUCTIVE_LIKE等 |
| fusion_task.cc | ✅ 已修改 | UpdateBreathWave/UpdateRadarComposite/UpdateCachedSnoreRespScore |
| sleep_data_center.h | ✅ 已修改 | RecordBreathEvent, UpdateWaveFeatures, cached_snore_resp_score_ |
| sleep_data_center.cpp | ✅ 已修改 | 全部新方法, UpdateSpo2时间差, UpdateBreathWave事件分级 |
| snore_resp_score.h | ✅ 已修改 | PATH_OBSTRUCTIVE_LIKE, PATH_CENTRAL_LIKE, data_completeness |
| snore_resp_score.cpp | ✅ 已修改 | 无血氧归一化, 建议缓冲bug修复, subtype字符串 |
| main_report_uart.cpp | ✅ 已修改 | 31字段, 6个扩展字段, wifi/sd改为2=未知 |
| snore_report_rx.cpp | ✅ 已修改 | ≥14字段兼容, SPO2行解析 |
| atk_dnesp32s3.cc | ✅ 已修改 | MCP 11工具, start/stop改回AddTool |
| CMakeLists.txt | ✅ 已修改 | 加snore_resp_score.cpp, .c→.cpp改名 |
| watch_ble_receiver.cpp | ❌ 禁用 | 不在CMakeLists, fusion_task注释掉 |
| sdkconfig | ⚠️ 手动改 | CONFIG_BT_ENABLED is not set |
