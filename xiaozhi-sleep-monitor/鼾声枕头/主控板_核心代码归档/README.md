# 小智 AI 睡眠监测伴侣 — 完整源代码

> 基于正点原子 ATK-DNESP32S3 (ESP32-S3) 的 AIoT 睡眠监测系统
>
> 集成本地 TFLM 神经网络鼾声检测、多传感器数据融合、MCP 设备控制协议、大模型语音交互

---

## 工程结构

```
xiaozhi_important_code/
├── CMakeLists.txt              # 顶层 CMake
├── sdkconfig                   # ESP-IDF 完整配置
├── sdkconfig.defaults.esp32s3  # ESP32-S3 默认配置
├── REVIEW_FINDINGS.md          # 代码审查结果
│
├── docs/
│   ├── README_PROJECT.md       # 项目说明（架构+功能+模块）
│   ├── DATA_FLOW_ANALYSIS.md   # 完整数据链路分析（8条链路）
│   ├── BUGFIX_LOG.md           # Bug 修复日志
│   ├── 答辩材料.md             # 毕设/比赛答辩材料
│   └── 鼾声分析与睡眠健康守护.md
│
├── scripts/                    # Python 工具脚本
│   ├── gen_sleep_reports.py
│   ├── gen_breath_event_test.py
│   ├── gen_breath_waveform.py
│   └── gen_intervention_comparison.py
│
└── main/                       # 全部源代码
    ├── CMakeLists.txt           # 主程序 CMake (所有源文件列表)
    ├── Kconfig.projbuild        # Kconfig 配置项定义
    ├── idf_component.yml        # ESP-IDF 组件依赖
    │
    ├── main.cc                  # 程序入口
    ├── application.cc/h         # ★ 主事件循环 (FreeRTOS EventGroup 驱动)
    ├── device_state_machine.cc/h # ★ 设备状态机 (Idle/Listening/Speaking)
    ├── device_state.h           # 状态枚举
    ├── fusion_task.cc/h         # ★ 融合任务 (每秒 tick，桥接所有数据源)
    ├── mcp_server.cc/h          # ★ MCP Server (JSON-RPC 2.0 设备能力暴露)
    ├── system_info.cc/h         # 系统信息
    ├── ota.cc/h                 # OTA 固件升级
    ├── settings.cc/h            # NVS 设置存储
    ├── assets.cc/h              # 资源管理
    ├── app_role_config.h        # 角色配置 (主板/UI板)
    ├── snore_report_rx.cpp/h    # ★ SNORE 报告接收 (UART2 RX)
    ├── main_report_uart.cpp/h   # ★ MAIN 报告发送 (UART2 TX)
    │
    ├── sleep_monitor/           # ★★ 端侧 AI 推理 (最核心创新)
    │   ├── model_interface.cpp/h    # TFLM 模型加载+推理 (10种算子注册)
    │   ├── feature_extractor.cpp/h  # MFCC 特征提取 (ESP-DSP FFT加速)
    │   ├── sleep_monitor_api.cpp/h  # 监测任务调度 (双FreeRTOS任务)
    │   ├── microphone_driver.cpp/h  # I2S 麦克风驱动
    │   ├── microphone_manager.cpp/h # 麦克风管理
    │   ├── model_data.h            # 量化模型权重 (C数组)
    │   ├── app_config.h            # 模型/音频参数配置
    │   └── dsp_utils.h             # DSP 工具宏
    │
    ├── sleep_data_center/       # ★★ 多源数据融合中心
    │   ├── sleep_data_center.cpp/h  # 统计仓库 (线程安全 mutex)
    │   ├── sleep_score.cpp/h        # 8维睡眠评分算法 (0-100分)
    │   ├── snore_resp_score.cpp/h   # 呼吸暂停专项评分 (rREI/T90/低氧负荷)
    │   ├── sleep_sd_logger.cpp/h    # SD卡数据持久化
    │   ├── uart_audio_rx.cpp/h      # UART 音频接收
    │   ├── sensor_uart_receiver.cpp/h # 传感器 UART 接收
    │   └── watch_ble_receiver.cpp/h # BLE 手表接收器 (已禁用)
    │
    ├── fusion/                  # ★★ 多证据融合引擎
    │   ├── sleep_fusion.c/h         # 核心融合引擎 (120s ring buffer + 状态机)
    │   ├── sleep_health_fusion.c/h  # 健康分析引擎
    │   ├── sleep_baseline.c/h       # 基线学习
    │   ├── sleep_algorithm.c/h      # 睡眠算法
    │   ├── sleep_radar_data.c/h     # 雷达数据管理 (波形缓冲300)
    │   ├── sleep_logger.c/h         # 融合日志
    │   ├── radar_breath_event.c/h   # 呼吸事件检测
    │   ├── radar_wave_analyzer.c/h  # 波形深度分析
    │   ├── snore_event_detector.c/h # 鼾声事件检测
    │   ├── r60abd1_adapter.c/h      # R60 雷达适配转换
    │   └── fusion_types.h           # 完整类型定义 (事件/特征/证据)
    │
    ├── drivers/                 # 硬件驱动
    │   └── r60abd1_uart.c/h         # R60 毫米波雷达 UART 协议解析
    │
    ├── sensors/                 # 板载传感器驱动
    │   ├── sht30.c/h               # I2C1 温湿度
    │   ├── ap3216c.c/h            # I2C0 光照/接近
    │   ├── temt6000.c/h           # 模拟光照
    │   └── chip_temp.c/h          # 芯片温度
    │
    ├── audio/                   # 音频处理流水线
    │   ├── audio_service.cc/h      # OPUS 编解码 + 音频管道
    │   ├── audio_codec.cc/h        # 音频 Codec 抽象
    │   ├── audio_processor.h       # 音频处理器接口
    │   ├── wake_word.h            # 唤醒词接口
    │   ├── README.md              # 音频模块文档
    │   ├── codecs/                # 各 Codec 实现 (ES8388/ES8311/ES8374等)
    │   ├── processors/            # 音频处理器 (AFE/Noop)
    │   ├── demuxer/               # 音频解复用
    │   └── wake_words/            # 唤醒词引擎 (AFE/Custom/ESP)
    │
    ├── protocols/               # 通信协议
    │   ├── protocol.cc/h          # 协议抽象基类
    │   ├── websocket_protocol.cc/h # WebSocket 实现
    │   └── mqtt_protocol.cc/h     # MQTT+UDP 混合协议
    │
    ├── display/                 # 显示子系统
    │   ├── display.cc/h           # 显示抽象
    │   ├── lcd_display.cc/h       # LCD (SPI ST7735/ST7789)
    │   ├── oled_display.cc/h      # OLED (I2C SH1106)
    │   ├── emote_display.cc/h     # 表情显示
    │   └── lvgl_display/          # LVGL 图形框架
    │       ├── lvgl_display.cc/h
    │       ├── lvgl_theme.cc/h     # 主题管理 (light/dark)
    │       ├── lvgl_font.cc/h      # 字体管理
    │       ├── lvgl_image.cc/h     # 图片管理
    │       ├── emoji_collection.cc/h # 表情集
    │       ├── gif/               # GIF 解码
    │       └── jpg/               # JPEG 编解码
    │
    ├── boards/                  # 开发板抽象层 (HAL)
    │   ├── atk-dnesp32s3/       # ★ 正点原子 ESP32-S3 (目标板)
    │   │   ├── atk_dnesp32s3.cc  # 板级初始化 + MCP工具注册
    │   │   ├── config.h          # 引脚配置
    │   │   └── config.json       # 板级配置
    │   └── common/               # 公共基类
    │       ├── board.cc/h        # Board 基类
    │       ├── wifi_board.cc/h   # WiFi 板基类
    │       └── ...               # 电池/按钮/背光等外设驱动
    │
    └── led/                     # LED 驱动
        ├── single_led.cc/h
        ├── circular_strip.cc/h
        └── gpio_led.cc/h
```

---

## 六大创新点

### 1. 端侧 AI 推理 (TFLM on ESP32)
- **模型**: 量化 CNN (Conv2D → MaxPool → FC → Logistic)
- **输入**: 49帧 × 13维 MFCC (3秒音频窗口, 50%重叠)
- **输出**: 鼾声概率 [0~1]
- **优化**: PSRAM 张量 arena、堆分配避免 static trap、10种算子精简注册
- **Fallback**: TFLM 不可用时自动降级为启发式 mock 推理

### 2. 多源证据融合引擎
- 120秒环形缓冲 (音频+雷达双通道)
- 滑动窗口 (10s/30s) 特征提取
- 状态机 + 硬性抑制规则 (体动优先、基线无效抑制)
- 证据标志位 (evidence_flags) 追踪每项判断依据

### 3. MCP 标准化设备协议
- JSON-RPC 2.0 完整实现 (initialize/tools/list/tools/call)
- 11 个 AI 可调用工具 (sleep 启停/查询/报告)
- 分页支持 (单包 ≤ 8KB)
- AI/User 工具隔离

### 4. 睡眠质量评分算法
- 8 维度综合评分 (SpO2/心率/呼吸/体动/鼾声/噪声/心率变异/呼吸节律)
- 呼吸暂停专项评分 (低氧负荷/T90/rREI/心率代偿)
- 风险等级 (低/中/高) + 个性化中文建议

### 5. 事件驱动架构
- FreeRTOS EventGroup 驱动主循环
- 双核任务分配 (音频采集 Core0, 推理 Core1)
- 每秒 tick 融合调度 (SNORE + 雷达 + 环境 + 评分)

### 6. 双板 UART 通信协议
- MAIN 报告: 主板 → UI板 (31字段 CSV)
- SNORE 报告: UI板 → 主板 (14+字段 CSV)
- 新鲜度检查 + 自动超时清零

---

## 硬件平台

| 组件 | 型号 | 接口 |
|------|------|------|
| 主控 | ESP32-S3 (正点原子 ATK-DNESP32S3) | - |
| 音频 | ES8388 Codec | I2S + I2C0 |
| 雷达 | R60ABD1 毫米波 | UART1 (GPIO16/17) |
| 温湿度 | SHT30 | I2C1 (GPIO4/16) |
| 光照 | AP3216C | I2C0 (GPIO41/42) |
| 显示屏 | ST7735 1.8" LCD | SPI2 |
| SD卡 | MicroSD | SPI3 |
| UI板通信 | UART2 (GPIO19/20) | 115200bps |

## 软件架构

```
云端: ASR + LLM + TTS ←→ WebSocket/MQTT ←→ ESP32 设备
                                              │
                    ┌─────────────────────────┼─────────────────────────┐
                    ▼                         ▼                         ▼
            AudioService              SleepMonitor              FusionTask
           (OPUS+AFE+唤醒)           (TFLM+MFCC推理)          (多源融合引擎)
                    │                         │                         │
                    └─────────────────────────┼─────────────────────────┘
                                              ▼
                                     SleepDataCenter
                                    (统计仓库+评分+SD日志)
                                              │
                                              ▼
                                       MCP Server
                                    (JSON-RPC 2.0)
```

## 开发环境
- ESP-IDF v5.4+
- 芯片: ESP32-S3
- 语言: C/C++ (C17/C++23)
- Python 3.x (脚本工具)
