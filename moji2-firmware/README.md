# Moji 2.0 桌面机器人固件（ESP32-C5）

[OSHWHUB Moji 2.0](https://oshwhub.com/movecall/moji2) 桌面机器人的自研固件骨架，
**为 DIY 修改而生**：全部代码只有几个小文件，没有协议栈包袱，想改哪改哪。

引脚与屏幕初始化序列已和作者官方固件（MoveCall/xiaozhi-esp32 fork 的
`movecall-moji2-esp32c5/config.h`）逐一核对过，可放心在此之上折腾。

## 硬件资源（已与官方固件 config.h 核对）

| 功能 | GPIO | 说明 |
|------|------|------|
| LCD SCK | IO0 | 1.5 寸 360x360 圆屏，QSPI（ST77916，40MHz） |
| LCD RESET | IO1 | |
| LCD 背光 | IO2 | 经三极管驱动，LEDC PWM 调光 |
| LCD CS | IO3 | |
| LCD D0~D3 | IO9 / IO8 / IO7 / IO6 | QSPI 四数据线 |
| I2S BCLK / WS / MCLK | IO11 / IO24 / IO25 | ES8311 音频编解码 |
| I2S DOUT / DIN | IO23 / IO12 | DOUT=喇叭通路，DIN=麦克风通路 |
| I2C SDA / SCL | IO26 / IO27 | ES8311 控制口（7bit 地址 0x18） |
| 功放使能 | IO5 | D 类功放 CTRL |
| 电池电压 | IO4 | VBAT 经 1:1 电阻分压，ADC1_CH3 |
| RGB 灯 | IO10 | TX1812（WS2812 协议），单颗 |
| BOOT 键 | IO28 | 外部上拉，按下为低 |
| USB | IO13 / IO14 | C5 内置 USB-Serial-JTAG，直连 Type-C |

## 已实现功能

- **表情系统**：程序绘制的大眼睛机器人表情（普通/开心/生气/困倦/聆听），周期性眨眼，
  按行带渲染 + QSPI DMA 刷屏，无需任何图片资源和字库
- **屏幕初始化**：使用官方固件中针对该批次屏幕的 ST77916 初始化序列
  （`st77916_init_cmds.h`，逐字移植）
- **音频**：ES8311 寄存器初始化、I2S 双工声道（24kHz/16bit，与官方一致）、
  功放使能控制、开机提示音，并提供 `bsp_audio_read_mic()` 麦克风采样接口
- **电源**：ADC 采集电池电压，屏幕顶部显示电量图标
- **Wi-Fi**：STA 连接（C5 双频自动扫描，建议路由器开 5GHz 同名 SSID），断线自动重连
- **交互**：BOOT 键短按循环切换表情，RGB 灯随表情变色

## 目录结构

```
esp32/
├── CMakeLists.txt          工程入口
├── sdkconfig.defaults      默认配置（target: esp32c5）
├── partitions.csv          分区表（factory 3MB）
└── main/
    ├── board.h             ★ 全部引脚定义（已对照官方 config.h）
    ├── main.c              主循环：眨眼动画 / 按键 / 电量 / Wi-Fi 事件
    ├── bsp_display.c       ST77916 QSPI 初始化 + 背光
    ├── st77916_init_cmds.h 官方屏幕初始化序列（逐字移植）
    ├── emoji.c             表情渲染器（想换表情风格就改这里）
    ├── bsp_audio.c         ES8311 + I2S + 功放
    ├── bsp_power.c         电池 ADC
    ├── bsp_led.c           WS2812 RGB 灯
    ├── wifi_mgr.c          Wi-Fi STA
    └── idf_component.yml   依赖：esp_lcd_st77916、led_strip
```

## 构建与烧录

需要 **ESP-IDF v6.0+**（本机 `~/esp/v6.0` 已装好工具链与 Python 3.13 环境）。
每次开新终端先执行（export.sh 依赖 Python 3.13，需把它放到 PATH 最前）：

```bash
export PATH="$HOME/esp/python313/python/bin:$PATH"
. ~/esp/v6.0/esp-idf/export.sh
```

然后：

```bash
cd ~/esp32
idf.py build           # sdkconfig.defaults 已含 target，无需 set-target
idf.py -p /dev/tty.usbmodemXXXX flash monitor
```

Wi-Fi 账号密码：`idf.py menuconfig` → Moji2 配置。

烧录口即板子 Type-C（USB-Serial-JTAG）。如识别不到，按住 BOOT 键再插 USB 进入下载模式。

## DIY 修改入口（想改什么 → 改哪个文件）

| 想改的东西 | 位置 |
|-----------|------|
| 表情长相（眼睛/嘴/腮红/眉毛） | `emoji.c` 顶部的布局参数和各 `draw_*` 函数 |
| 新增一种表情 | `emoji.h` 的 `emoji_expr_t` 加枚举 → `emoji.c` 实现 → `main.c` 循环表加一项 |
| 表情对应的灯颜色 | `main.c` 的 `s_expr_led[]` |
| 眨眼频率/速度 | `main.c` 主循环里的 `next_blink`、`* 0.4f` 平滑系数 |
| 提示音 | `bsp_audio.c` 的 `bsp_audio_play_boot_sound()` |
| 麦克风增益 / 喇叭音量 | `bsp_audio.c` 初始化表的 0x17 / 0x32 寄存器 |
| 背光默认亮度 | menuconfig → Moji2 配置，或 `bsp_display.c` |
| 按键行为 | `main.c` 的 `button_clicked()` 调用处 |
| 引脚 | `board.h`（一般不用动） |

## 参考：官方固件

作者的小智完整固件在 [MoveCall/xiaozhi-esp32](https://github.com/MoveCall/xiaozhi-esp32)
（`main/boards/movecall-moji2-esp32c5/`）。本工程的引脚和屏幕初始化序列即取自那里，
DIY 时若某个硬件行为不确定，可去对照它的实现。

## 自定义表情

- 简单改法：编辑 `emoji.c` 里的几何参数（眼睛位置/大小、嘴型、腮红），或往
  `emoji_expr_t` 加新表情并在 `draw_mouth()`/`render_row()` 里实现。
- 图片/GIF 表情：在 `sdkconfig.defaults` 里打开 PSRAM（已留注释），接入 LVGL +
  `lv_gif`，保持 `emoji_scene_t` 接口不变即可无缝替换渲染实现。

## 注意事项

1. 本工程代码**尚未在真机上编译/运行验证**（应要求只写代码）。引脚、屏幕初始化序列、
   电量分压比已与官方固件核对一致；ES8311 初始化表是 esp-adf 通用序列，
   麦克风增益（0x17）、DAC 音量（0x32）可能需按听感微调。
2. **充电状态**：充电指示由充电芯片直接驱动 LED，未接 GPIO，固件无法读取充电状态
   （官方固件同样如此，充电检测脚为 GPIO_NUM_NC）。
3. 若屏幕颜色发灰/反相，打开 `bsp_display.c` 中注释掉的 `esp_lcd_panel_invert_color()`。
