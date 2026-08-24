# 小智主板 睡眠融合模块（双板架构 · 主板端）

## 硬件角色

| 板子 | 职责 |
|------|------|
| **小智主板** (ATK DNESP32S3) | R60 雷达、融合分析、MCP、WiFi、语音、发送 MAIN |
| **UI 板** (ESP32-S3) | INMP441 麦克风、本地鼾声模型、发送 SNORE、显示 UI |

## 硬件连线

### R60 毫米波雷达 (UART1)
```
R60 TX  → 小智 GPIO17 RX
R60 RX  → 小智 GPIO18 TX
R60 VCC → 5V
R60 GND → GND
```

### UI 板通讯 (UART2, 双向)
```
UI TX    → 小智 GPIO19 RX  (收 SNORE)
UI RX    → 小智 GPIO20 TX  (发 MAIN)
UI GND   → 小智 GND
```

## 文件清单

```
├── app_role_config.h         # 角色配置（功能开关 + UART 引脚分配）
├── CMakeLists.txt            # 编译配置（需合并到 main/CMakeLists.txt）
├── application.cc            # 主初始化（需合并到 main/application.cc）
│
├── snore_report_rx.h/.c      # 收 UI 板 SNORE 文本 → audio_feature_t
├── main_report_uart.h/.c     # 发 MAIN 文本给 UI 板
├── fusion_task.h/.cc         # 融合主任务（1Hz tick, 胶水层）
│
├── sensors/                  # 板载传感器（当前禁用，待 I2C API 适配）
│   ├── sht30.h/.c            # SHT30 温湿度 (I2C0 0x44)
│   ├── ap3216c.h/.c          # AP3216C 光照 (I2C0 0x1E)
│   └── chip_temp.h/.c        # ESP32 内置温度
│
├── drivers/
│   └── r60abd1_uart.h/.c     # R60ABD1 毫米波雷达 UART 驱动
│
├── fusion/                   # 融合引擎（从 UI 工程复用）
│   ├── fusion_types.h        # 统一类型定义
│   ├── sleep_fusion.h/.c     # 音频+雷达融合风险筛查
│   ├── sleep_health_fusion.h/.c  # 健康风险分析
│   ├── sleep_baseline.h/.c   # 个人基线采集
│   ├── radar_breath_event.h/.c   # 呼吸事件检测
│   ├── snore_event_detector.h/.c # 鼾声事件检测
│   ├── r60abd1_adapter.h/.c  # R60 数据 → radar_feature_t 转换
│   ├── sleep_radar_data.h/.c # 雷达数据管理
│   ├── sleep_algorithm.h/.c  # 算法工具
│   └── sleep_logger.h/.c     # 日志工具
│
└── boards/atk-dnesp32s3/
    ├── atk_dnesp32s3.cc      # 板级配置 + MCP 工具
    └── config.h              # 引脚定义
```

## 数据流

```
每秒 fusion_tick:
  1. snore_report_rx → audio_feature_t → sleep_fusion_feed_audio
  2. R60 雷达 → radar_feature_t → sleep_fusion_feed_radar
  3. sleep_fusion_tick / sleep_health_fusion_tick
  4. SleepDataCenter 更新
  5. main_report_uart_send → MAIN 文本 → UART2 TX → UI 板
```

## MCP 工具（不变）

8 个睡眠 MCP 工具：start / stop / status / get_vitals / get_snore_status / get_environment / get_apnea_risk / generate_report

## 协议

### UI→主板: SNORE (115200 bps, 1Hz)
```
SNORE,ts_ms,mic_ok,audio_valid,snore_active,snore_score,snore_db,rms,peak,zcr_x100,current_episode_ms,snore_total_ms,longest_episode_ms,snore_episode_count,quality
```

### 主板→UI: MAIN (115200 bps, 1Hz)
```
MAIN,ts_ms,radar_status,heart_valid,heart_bpm,breath_valid,breath_bpm,motion_valid,body_motion,bed_valid,in_bed,stage_valid,sleep_stage,risk_valid,risk_level,event_id,apnea_count,hypopnea_count,system_status,wifi_ok,sd_ok,spo2_valid,spo2,temp_valid,temp_x10,hum_x10
```

## 编译

```bash
idf.py build
idf.py -p COM15 flash monitor
```
