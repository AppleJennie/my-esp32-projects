# AI 睡眠床头监护仪 — 核心代码归档

## 工程概要

- **平台**: ESP32-S3 + ALIENTEK 4.3寸 RGB LCD (800x480)
- **框架**: ESP-IDF v5.5.4 + LVGL v8.4.0
- **AI引擎**: TensorFlow Lite Micro + ESP-NN
- **传感器**: INMP441 MEMS 麦克风 (16kHz I2S)

## 核心创新点

### 1. 混合 AI + 规则 鼾声检测架构
TFLite Micro 模型输出 0~255 的"鼾声相似度"分数，
P1/P2 状态机在此基础上做稳定化决策，
规则型分类器 (snore_classifier) 进一步做鼻/喉/口/混 四分类。
三者叠加形成"AI感知 + 规则决策 + 频谱分类"的完整链路。

### 2. Micro Frontend 音频特征提取
从 TFLite Micro 官方 micro_speech 示例移植的完整前端链：
Hamming窗 → FFT → Mel滤波组 → PCAN增益控制 → Log Scale → 量化
最终产出 1830 维 uint8 特征向量送入 TFLite 推理。

### 3. 声学频谱分析 (snore_audio_analyzer)
独立于 TFLite 的并行分析通道：
- 频谱质心、低频能量比、谐波比
- 基于谐波结构的鼾声分类 (鼻型/喉型/口型/混型)
- 气流声检测、恢复呼吸检测
- RMS/Peak/ZCR 多维度门控

### 4. 自定义 LVGL 睡眠监护仪表盘
深色主题 (GitHub-dark), 7 页面:
- 首页仪表盘 (睡眠评分弧线 + OV-Watch 风格指标卡片)
- 实时监护 / 波形 / 报告 / 事件 / AI建议 / 设置

### 5. 双板 UART 协议
本板(NODE) 采集音频+鼾声AI, 对端主板(MAIN) 负责雷达+融合,
通过 UART2 双向传输 snore_report / main_report。

---

## 目录结构

```
_核心代码归档/
├── README.md                          ← 本文件
│
├── 01_应用层_UI界面/                  ← 主入口 + LVGL UI + 数据模型
│   ├── main.c                          # app_main() 入口
│   ├── lvgl_demo.c / .h               # LVGL 初始化, display/indev 移植
│   ├── sleep_ui.c / .h                # ★ 核心UI: 7页面仪表盘 + 刷新逻辑
│   ├── sleep_data.c / .h              # SleepData_t 数据结构
│   ├── control_panel.c / .h           # 下拉快捷控制面板
│   ├── sleep_assistant_ui.c / .h      # 睡眠助手页面
│   ├── sleep_player_ui.c / .h         # 助眠播放器页面
│   ├── hardware_control.c / .h        # FreeRTOS 硬件命令队列
│   ├── fusion_types.h                 # 传感器融合共享类型
│   ├── sleep_project_config.h         # ★ 项目硬件功能开关
│   └── app_config.h                   # 外设启用宏
│
├── 02_音频采集_MIC驱动/               ← INMP441 驱动 + I2S
│   ├── inmp441_i2s.c / .h             # MEMS 麦克风: 环形缓冲, DC阻断, 有效性检测
│   └── audio_i2s.c / .h              # 通用 I2S 驱动
│
├── 03_AI推理_鼾声检测核心/            ← 音频管道 + 分类器
│   ├── audio_pipeline.cpp / .h        # ★ 核心管道: 窗口选择 → TFLite推理 → 状态机
│   ├── audio_processor.c / .h         # 音频预处理: FFT/Hamming/归一化
│   ├── snore_classifier.cpp / .h      # ★ 规则型鼾声四分类器
│   ├── model_config.h                 # TFLite 输入输出规格
│   └── snore_detection.tflite         # TFLite 模型文件
│
├── 04_Micro模型_特征提取/             ← TFLite Micro 推理 + 前端处理
│   ├── snore_detector_micro.cpp / .h  # ★ Micro 模型推理封装
│   ├── snore_micro_feature_adapter.cpp/.h # 特征提取适配器
│   ├── snore_model_micro_data.h       # 模型权重数据
│   └── microfrontend/                 # ★ 完整音频前端链
│       ├── frontend.c / .h            # 前端总控
│       ├── fft.cpp / .h              # FFT (kiss_fft 封装)
│       ├── filterbank.c / .h          # Mel 滤波器组
│       ├── noise_reduction.c / .h     # 噪声抑制
│       ├── pcan_gain_control.c / .h   # PCAN 自动增益控制
│       ├── log_scale.c / .h          # Log 尺度变换
│       └── window.c / .h             # 加窗函数
│
├── 05_鼾声分析_事件检测/              ← 频谱分析 + 事件合并
│   ├── snore_audio_analyzer.c / .h    # ★ 频谱分析: 质心/谐波/分类/气流
│   ├── snore_event_detector.c / .h    # 鼾声事件合并 + 统计
│   ├── sleep_monitor_data_adapter.c/.h # 数据适配: 维护类型计数
│   └── sleep_audio_adapter.c / .h     # 音频 ↔ 睡眠数据桥接
│
├── 06_板级驱动_LCD_Touch/             ← 硬件底层驱动
│   ├── ltdc.c / .h                   # LTDC RGB 控制器 (含引脚定义)
│   ├── lcd.c / .h                    # LCD 面板命令
│   ├── touch.c / .h / gt9xxx.h       # GT9xxx 触摸驱动
│   ├── xl9555.c / .h                 # XL9555 GPIO 扩展器
│   └── iic.c / .h                    # I2C 总线驱动
│
└── 07_构建配置/                       ← ESP-IDF 构建
    ├── CMakeLists.txt                 # main 组件注册
    ├── idf_component.yml              # lvgl, esp-nn, esp-tflite-micro 依赖
    └── partitions-16MiB.csv          # 16MiB Flash 分区表
```

## 关键数据流

```
INMP441 → RingBuffer(4s) → 窗口选择(评分) → MicroFrontend(1830维) 
→ TFLite推理(snore_score 0-255) → P1/P2状态机(is_snoring YES/NO)
→ snore_classifier(鼻/喉/口/混) + snore_audio_analyzer(频谱)
→ snore_event_detector(事件合并/统计) → SleepData_t → LVGL UI刷新
```

## 鼾声检测决策链 (P1/P2 状态机)

```
模型输出 snore_score (0~255)
    │
    ├─ ≥140 且 ZCR<0.25 → candidate_hit
    │       3帧中≥1帧命中 → 进入 SNORE_ACTIVE
    │
    ├─ ≥80  → weak_hit (维持活跃)
    │
    ├─ <80  → no_hit (计入off计数)
    │       连续2帧 → 退出 ACTIVE
    │
    └─ 最后一次命中后保持 10 秒 (HOLD)
```
