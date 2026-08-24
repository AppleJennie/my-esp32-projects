# MoveCall Moji（魔镜）AI 语音助手硬件复刻分析

> 分析日期：2026-08-24
> 分析对象：`MoveCall/xiaozhi-esp32`（GitHub fork）、立创开源硬件平台 `oshwhub.com/movecall/moji2`
> 对比基准：本机 `/home/applejennie/esp/xiaozhi-esp32`（上游 78/xiaozhi-esp32 v2.4.2 副本）
>
> **重要限制说明**：本机网络无法直连 github.com（Connection refused），gitclone.com（504）、ghproxy.com（超时）两个镜像也失败，三次克隆均未成功（详见 `/home/applejennie/esp/project/BLOCKED.md`）。GitHub 侧分析基于 FetchURL 抓取的 raw 文件与 GitHub API；oshwhub 的 git 克隆返回 "repository not found"（需登录），但其项目**网页可公开访问**，硬件细节主要来自该网页。

---

## 1. 项目概述

**Moji** 是作者 **movecall**（立创开源硬件平台 / GitHub: MoveCall）设计的"小智 AI 衍生版"桌面机器人，基于 78/xiaozhi-esp32 固件。按作者在 oshwhub 的描述：

- **Moji 1.0**：首代产品，ESP32-S3 主控，圆形小屏，屏幕需要手工焊接，定位"硬核试炼"。
- **Moji 2.0**（oshwhub 标题："Moji 2.0 小智AI桌面机器人 5G Wi-Fi 长续航"）：针对 1.0 复刻者最头疼的"屏幕焊接"问题重新设计——首发采用 ESP32-C5、升级 1.5 寸 360×360 高清圆屏、屏幕改为 **FPC 插接（ZIF 零插拔力插座）免焊**，内置 500mAh 电池 + DC-DC 电源 + 预留无线充电焊盘，配合小智 AI 2.0 支持自定义表情（GIF/图片按情绪切换）。作者明确说明 **2.0 与 1.0 外壳、屏幕、主板完全不通用**。

出处：

- 固件 fork：https://github.com/MoveCall/xiaozhi-esp32
- 硬件开源页（2.0）：https://oshwhub.com/movecall/moji2 （板级 README 中给出的链接）
- 3D 外壳：作者称在 MakerWorld 发布
- MoveCall 在该 fork 里还有第三块板 `movecall-cuican-esp32s3`（璀璨·AI 吊坠），与 moji 无关，本文不展开。

fork 的 `main/boards/` 下共有两个 moji 板目录：

| 目录 | 对应产品 | 主控 |
|---|---|---|
| `movecall-moji-esp32s3` | Moji 1.0 | ESP32-S3 |
| `movecall-moji2-esp32c5` | Moji 2.0 | ESP32-C5 |

---

## 2. 硬件方案

以下内容以板目录中 `config.h` 引脚宏和板级 `.cc` 实际初始化代码为准；oshwhub 网页信息单独标注。

### 2.1 Moji 2.0（`movecall-moji2-esp32c5`）—— 重点

- **主控**：ESP32-C5（RISC-V 单核 240MHz，Wi-Fi 6 双频 2.4/5GHz + BLE 5）。`config.json` 目标 `esp32c5`，sdkconfig 追加：16MB Flash（fork 版显式指定 `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y` + `partitions/v2/16m.csv`）、`CONFIG_SPIRAM=y`（QUAD、80MHz）、tickless idle、C5 240MHz flash 频率限制。
  - ESP32-C5 本身可配外部 PSRAM（乐鑫官方 DevKitC-1 及 WROOM-1 模组均有 8MB PSRAM 版本），固件启用了 QUAD PSRAM，**复刻时模组须选带 PSRAM 的型号（具体模组料号需原理图确认）**。
- **屏幕**：1.5 寸 360×360 圆形 IPS 屏，驱动 **ST77916**，**QSPI（4 线）接口** 40MHz，`SPI2_HOST`，RGB565。初始化序列为 ST77916 厂方初始化码（`lcd_init_cmds[]`，含 QSPI 模式切换 `use_qspi_interface=1`）。oshwhub 注明必须买 "1.5 寸 QSPI 接口 360×360 圆屏"，SPI/MIPI 接口不兼容；FPC/ZIF 插接。
- **音频**：**ES8311** codec（I2C 地址 `ES8311_CODEC_DEFAULT_ADDR`），输入/输出均 24kHz，外接 PA 功放（使能脚 GPIO5）。oshwhub 称采用"ES8311 全差分链路"降低底噪（作者自注"其实效果不怎么样"）。麦克风为 ES8311 配套模拟麦克风（型号需原理图确认），作者警告麦克风极度怕热（风枪 ≤300°C、<10s，禁洗板水渗入）。
- **按键**：单个 BOOT 按键（GPIO28）。单击：启动中进入配网 / 运行中切换对话状态；支持 **PressToTalkMcpTool**（按下说话、松开停止，可通过 MCP 工具切换模式）。
- **LED**：单色 LED（GPIO10，`SingleLed`）。
- **电池/电源**：`AdcBatteryMonitor(ADC_UNIT_1, ADC_CHANNEL_3, 5100000, 5100000, GPIO_NUM_NC)` —— ADC1_CH3 在 ESP32-C5 上固定映射 **GPIO4**，分压电阻 5.1MΩ/5.1MΩ（1/2 分压），**无充电状态检测引脚（NC）**。`PowerSaveTimer(240, -1, -1)`：240 秒无操作进省电（关屏），充电时禁用省电定时器。oshwhub：500mAh 锂电（BOM 表列 602040 550mAh）、DC-DC Buck、Type-C 充电、预留无线充焊盘、侧边物理滑动开关（0 功耗待机）。
- **结构**（oshwhub）：主板双层板、**必须 1.6mm 厚度**（否则 Type-C/开关位置与外壳不匹配）、尺寸 21×33.5mm、彩色 PCB；0.8mm 透明亚克力面板 + 3M 胶；3D 打印外壳 + 导光柱。

引脚分配（`config.h`）：

| 功能 | 引脚 |
|---|---|
| I2S MCLK / WS / BCLK / DIN / DOUT | GPIO25 / 24 / 11 / 12 / 23 |
| Codec I2C SDA / SCL | GPIO26 / 27 |
| PA 使能 | GPIO5 |
| QSPI 屏 SCLK / RESET / D0 / D1 / D2 / D3 / CS | GPIO0 / 1 / 9 / 8 / 7 / 6 / 3 |
| 背光（PWM） | GPIO2 |
| 内置 LED | GPIO10 |
| BOOT 按键 | GPIO28 |
| 电池电压检测 | ADC1_CH3（= GPIO4），1/2 分压 |

### 2.2 Moji 1.0（`movecall-moji-esp32s3`）

- **主控**：ESP32-S3（双核 Xtensa 240MHz，Wi-Fi 4 + BLE 5；模组 PSRAM/Flash 配置走工程默认值，`sdkconfig_append` 为空——按上游 S3 默认即 8MB Octal PSRAM + 8MB Flash，具体以所选模组为准）。
- **屏幕**：240×240 圆屏，驱动 **GC9A01**，普通 **4 线 SPI** 40MHz（`SPI3_HOST`），BGR、色彩反转、mirror_x。UI 针对圆屏给状态栏加了左右 33% 内边距（`CustomLcdDisplay::SetupUI()`）。
- **音频**：同款 **ES8311** + PA（使能 GPIO9），24kHz。
- **按键**：单 BOOT 键（GPIO0），仅单击（配网/切换对话），**无按下说话、无省电定时器**。
- **LED**：单色 LED（GPIO21）。
- **电池**：**代码中无任何电量检测 / 电源管理**（无 AdcBatteryMonitor、无 PowerSaveTimer），1.0 大概率无电池或纯直通供电（需原理图确认）。

引脚分配（`config.h`）：

| 功能 | 引脚 |
|---|---|
| I2S MCLK / WS / BCLK / DIN / DOUT | GPIO6 / 12 / 14 / 13 / 11 |
| Codec I2C SDA / SCL | GPIO5 / 4 |
| PA 使能 | GPIO9 |
| SPI 屏 SCLK / MOSI / CS / DC / RESET | GPIO16 / 17 / 15 / 7 / 18 |
| 背光（PWM） | GPIO3 |
| 内置 LED | GPIO21 |
| BOOT 按键 | GPIO0 |

### 2.3 C5 版 vs S3 版差异小结

| 项目 | Moji 1.0（S3） | Moji 2.0（C5） |
|---|---|---|
| 主控 | ESP32-S3 双核 240MHz，Wi-Fi 4 | ESP32-C5 单核 240MHz RISC-V，**Wi-Fi 6 双频（5GHz 降语音延迟）** |
| PSRAM | S3 默认 8MB Octal | 需选配带 PSRAM 模组，固件按 QUAD 80MHz 配置 |
| 屏幕 | GC9A01 240×240，普通 SPI，**需手焊** | ST77916 360×360 1.5 寸，**QSPI + FPC 免焊插接** |
| 音频 | ES8311 + PA | ES8311 + PA，差分走线重构 |
| 电池 | 无电量管理代码 | 500mAh + ADC 电量检测 + 240s 省电定时器 + 滑动电源开关 + 无线充预留 |
| 交互 | 单击对话 | 单击 + 按住说话（MCP 可切换） |
| 唤醒 | S3 支持 AFE 离线唤醒/打断 | C5 用 esp_wake_word（无 AFE），oshwhub FAQ 明确"**对话中不支持语音打断**" |
| 外壳 | 与 2.0 完全不通用 | 重新设计（含导光柱、亚克力面板） |

---

## 3. BOM 物料清单

来源标注：**[代码]** = 由板级代码/config.h 确认；**[oshwhub]** = 由 oshwhub 项目页确认；其余标"需原理图确认"。

| # | 元件 | 型号/规格 | 数量 | 来源/备注 |
|---|---|---|---|---|
| 1 | 主控模组（2.0） | ESP32-C5（WROOM-1 类，须带 PSRAM，16MB Flash） | 1 | [代码] target/PSRAM 配置；具体料号需原理图确认 |
| 2 | 主控模组（1.0） | ESP32-S3（建议 N8R8） | 1 | [代码] |
| 3 | 圆屏（2.0） | 1.5 寸 360×360 QSPI，ST77916 驱动，FPC 排线 | 1 | [代码]+[oshwhub]（约 ¥11.5） |
| 4 | 圆屏（1.0） | 240×240 SPI 圆屏，GC9A01 驱动 | 1 | [代码] |
| 5 | 音频 Codec | ES8311（QFN 封装，需热风枪/加热台焊接） | 1 | [代码]+[oshwhub] |
| 6 | D 类功放 | PA（使能脚 GPIO5/GPIO9 控制） | 1 | [代码] 存在使能脚；型号需原理图确认 |
| 7 | 模拟麦克风 | ES8311 配套 mic（怕热，≤300°C/<10s） | 1 | [oshwhub]；型号需原理图确认 |
| 8 | 喇叭 | 2828 4Ω 3W（2P 1.25 端子） | 1 | [oshwhub]（约 ¥4.2） |
| 9 | 锂电池 | 602040 550mAh（规格区写 500mAh） | 1 | [oshwhub]（约 ¥4） |
| 10 | 充电/电源 | Type-C 充电 + DC-DC Buck，预留无线充焊盘 | 1 套 | [oshwhub]；充电 IC 型号需原理图确认 |
| 11 | 电源开关 | 侧边滑动开关（0 功耗待机） | 1 | [oshwhub] |
| 12 | BOOT 按键 | 轻触按键（GPIO28 / GPIO0） | 1 | [代码] |
| 13 | 状态 LED | 单色 LED（GPIO10 / GPIO21） | 1 | [代码] |
| 14 | 屏幕插座 | ZIF/FPC 插座（免焊插接） | 1 | [oshwhub] |
| 15 | 电池分压电阻 | 5.1MΩ ×2（1/2 分压，接 GPIO4） | 2 | [代码]（仅 2.0） |
| 16 | 主板 PCB | 双层板，**1.6mm 厚**，21×33.5mm | 1 | [oshwhub]（PCBA 物料估算 ¥58.88/5pcs 均摊） |
| 17 | 亚克力面板 | 0.8mm 透明亚克力 46.3×46.3mm + 3M9448A | 1 | [oshwhub]（约 ¥3） |
| 18 | 外壳 + 导光柱 | 3D 打印（MakerWorld 下载） | 1 套 | [oshwhub]（成品喷漆件约 ¥22.7，DIY 可自备） |
| 19 | 螺丝/螺母/脚垫 | KM1.4×8、M2.6×6、M1.4 贴片螺母、18mm 脚垫 | 若干 | [oshwhub] |

oshwhub 给出的 2.0 整机成本估算：**标准版约 ¥104.89，DIY 版约 ¥74.83**（不含加工/运费/起订量）。

---

## 4. 固件结构

- **板级代码组织**：遵循 xiaozhi 标准板目录结构，每板一个目录，含 `config.h`（引脚宏）、板级 `.cc`（继承 `WifiBoard`，重写 `GetAudioCodec/GetDisplay/GetLed/GetBacklight` 等虚函数）、`config.json`（构建变体）、README。**目录内没有独立 CMakeLists**，板源码由 `main/CMakeLists.txt` 按 `BOARD_TYPE` glob 进工程。
- **DECLARE_BOARD 位置**：
  - C5 版：`main/boards/movecall-moji2-esp32c5/movecall_moji2_esp32s3.cc` 末尾 `DECLARE_BOARD(MovecallMoji2ESP32C5)`（注意文件名遗留叫 `esp32s3`，实际类名和内容都是 C5 版，属于命名疏忽，不影响功能）。
  - S3 版：`main/boards/movecall-moji-esp32s3/movecall_moji_esp32s3.cc` 末尾 `DECLARE_BOARD(MovecallMojiESP32S3)`。
- **Kconfig 注册**：fork 的 `main/Kconfig.projbuild` 中 `BOARD_TYPE_MOVECALL_MOJI_ESP32S3`（"Movecall Moji 小智AI衍生版"，依赖 `IDF_TARGET_ESP32S3`）和 `BOARD_TYPE_MOVECALL_MOJI2_ESP32C5`（"Movecall Moji2.0 小智AI衍生版"，依赖 `IDF_TARGET_ESP32C5`）。
- **ESP-IDF 版本**：板级 README 明确 **v5.5**；fork 的 `main/idf_component.yml` 要求 `idf: '>=5.5.2'`。显示屏驱动依赖组件 `espressif/esp_lcd_st77916`（fork 锁 `^1.0.1`）和 `espressif/esp_lcd_gc9a01`。
- **构建命令**（参照本工程 `scripts/build.py` 用法）：

  ```sh
  # 基于 fork（扁平目录布局）
  python3 scripts/build.py movecall-moji2-esp32c5 --name movecall-moji2-esp32c5
  python3 scripts/build.py movecall-moji-esp32s3   --name movecall-moji-esp32s3

  # 基于上游 v2.4.2+（制造商子目录布局，板已合并进上游，见第 5 节）
  python3 scripts/build.py movecall/moji2-esp32c5 --name movecall-moji2-esp32c5
  python3 scripts/build.py movecall/moji-esp32s3  --name movecall-moji-esp32s3
  ```

  也可按板 README 用原生 `idf.py set-target esp32c5 && idf.py menuconfig`（Xiaozhi Assistant → Board Type → Movecall Moji2.0）→ `idf.py build flash monitor`。
- **烧录**（oshwhub）：建议 ESP Flash Download Tool，地址 `0x00000`；按住 BOOT 键插入 USB 进烧录模式。

---

## 5. 与上游 78/xiaozhi-esp32 的差异

> 受克隆失败限制，无法做全量 `diff -rq`；以下基于 fork 根 README、`main/Kconfig.projbuild`、`main/CMakeLists.txt`、`main/idf_component.yml` 及板目录与本机上游 v2.4.2 副本的逐项对比。

- **fork 的根 README 与上游完全一致**（未做任何品牌定制，仍是 "An MCP-based Chatbot"）。
- **fork 相对上游的核心改动就是新增 3 块 MoveCall 板**（moji S3、moji2 C5、cuican S3）及配套的 Kconfig/CMakeLists 注册项。
- **这些板已被合并回上游**：本机上游 v2.4.2 副本已包含 `main/boards/movecall/{moji-esp32s3, moji2-esp32c5, cuican-esp32s3}`，且内容同源——两个 `config.h` 与 fork **逐字节一致**，板级 `.cc` 仅有一处等价重构（ST77916 的 panel IO 从 `ST77916_PANEL_IO_QSPI_CONFIG` 宏展开为显式 `io_config` 字段赋值，功能相同）。
- 合并进上游时的适配性改动：
  - 目录从 fork 的扁平 `main/boards/movecall-moji2-esp32c5/` 变为上游的制造商子目录 `main/boards/movecall/moji2-esp32c5/`；
  - `config.json` 增加 `"manufacturer": "movecall"` / `"type"` 字段；上游版去掉了 fork 中显式的 `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y` 和 `partitions/v2/16m.csv` 两行（改由上游 v2 分区/构建脚本统一处理）；
  - 组件版本随上游升级：`esp_lcd_st77916 ^1.0.1 → ^2.0.2`、`esp_lcd_gc9a01 ==2.0.1 → ~2.0.4`。
- 结论：**复刻不必使用该 fork，直接用上游 78/xiaozhi-esp32（≥v2.4.2 或含 movecall 板的版本）即可构建 moji 固件**，还能获得上游后续维护。fork 的价值主要在于板 README 中的构建说明。

---

## 6. 复刻建议

**推荐复刻 Moji 2.0（ESP32-C5 版）**，理由：

1. **装配难度显著更低**——这正是 2.0 的设计目标：屏幕 FPC/ZIF 插接免焊（1.0 最大痛点是手焊屏幕排线）；作者给出的复刻难度自评为 1 星。
2. **资料更完整**：oshwhub 页面有完整的安装步骤、BOM 成本表（约 ¥75–105）、FAQ 与避坑指南（ES8311 QFN 必须热风枪、麦克风怕热、PCB 必须 1.6mm、先验喇叭后焊麦克风）。
3. **体验更好**：1.5 寸 360×360 QSPI 屏动画流畅、支持自定义表情；Wi-Fi 6 5GHz 降低语音延迟；有电池、电量显示和省电管理，是完整的"桌面机器人"形态。
4. **固件维护性好**：板已进上游，直接用上游仓库构建即可，不必维护 fork。

**S3 版（1.0）适合的场景**：手头已有 ESP32-S3 模组和 GC9A01 圆屏、想做最省钱的纯验证（S3 模组和 240×240 GC9A01 屏都是最成熟好买的料，且 S3 的 AFE 离线唤醒/打断能力反而强于 C5）。但要接受手焊屏幕、无电池管理、外壳与 2.0 不通用。

**共同难点（两版都有）**：ES8311 为 QFN 封装，烙铁难焊、需热风枪/加热台；麦克风焊接温控苛刻（≤300°C、<10s）。

**待确认事项（需 oshwhub 工程附件中的原理图/Gerber，本机无法登录 oshwhub 下载）**：

- ESP32-C5 模组具体料号（须带 PSRAM 与 16MB Flash）；
- PA 功放、充电 IC、DC-DC、麦克风的具体型号；
- 电池容量 500mAh 与 602040/550mAh 两种表述以实物为准；
- Moji 1.0 是否有电池（固件无电量管理代码，推测无或简化处理）。

---

## 附：信息来源

- fork 板目录（raw 抓取）：`main/boards/movecall-moji2-esp32c5/{README.md, README_zh.md, config.h, config.json, movecall_moji2_esp32s3.cc}`、`main/boards/movecall-moji-esp32s3/{README.md, config.h, config.json, movecall_moji_esp32s3.cc}`
- fork 顶层：`README.md`、`main/Kconfig.projbuild`、`main/CMakeLists.txt`、`main/idf_component.yml`（raw 抓取）
- 本机上游副本：`/home/applejennie/esp/xiaozhi-esp32/main/boards/movecall/`（用于逐字节比对）
- oshwhub 项目页：https://oshwhub.com/movecall/moji2 （网页抓取；git 克隆被拦截，需登录）
- ESP32-C5 ADC 通道映射、PSRAM 支持：乐鑫官方文档与模组资料（网络检索）
