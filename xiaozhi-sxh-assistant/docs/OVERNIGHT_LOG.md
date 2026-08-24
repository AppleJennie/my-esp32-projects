# OVERNIGHT LOG — 2026-08-24/25 夜间任务

> 记录人：Kimi Code Agent。本文件随任务推进持续更新。

## 任务6.1 云服务器部署 xiaozhi-esp32-server（最简化 Docker）

**服务器**：腾讯云轻量 Ubuntu 22.04.5，2核 / 1.9G 内存 / 40G 磁盘，公网 `101.42.200.249`

**已完成**：

- Docker 29.7.2 + Compose v5.5.0（docker-ce 官方源，走 mirrors.cloud.tencent.com）
- swap：服务器已有 3G `/swapfile`（满足 ≥2G 要求）
- 部署目录 `~/xiaozhi-server/`：`docker-compose.yml`（容器内存上限 `mem_limit: 1200m`）、`data/.config.yaml`、`data/.wakeup_words.yaml`、`models/sherpa-sensevoice/`（sherpa 模型持久化挂载）
- 镜像 `ghcr.nju.edu.cn/xinnan-tech/xiaozhi-esp32-server:server_latest`（v0.9.6）拉取成功，容器 `restart: always` 运行中
- 运行时内存：约 745MiB / 1.2G 上限（62%），系统有余量

**模块配置**（`data/.config.yaml`，全免费方案）：

| 模块 | 选择 | 备注 |
|---|---|---|
| VAD | SileroVAD | 镜像内置，几 MB |
| ASR | **SherpaASR**（sherpa-onnx sense_voice int8，228M） | ⚠️ 与任务书"全 API"有偏差，原因见下 |
| LLM | ChatGLMLLM（glm-4.5-flash，智谱免费） | key 沿用本地配置 |
| TTS | EdgeTTS（zh-CN-YunyangNeural 云扬） | 兜底音色，理由见 6.3 节 |
| Memory / Intent | mem_local_short / function_call | 免费 |

**ASR 偏差说明**：任务书要求 ASR 走云端 API（如 DoubaoASR），但火山引擎 ASR 需要 AppID/Token，要明天开通才有。SherpaASR 是 onnx int8 小模型（非 FunASR 的 torch 大模型），实测内存可承受，先用它跑通端到端；明天火山 key 到手后在 `.config.yaml` 把 `selected_module.ASR` 换成 `DoubaoASR` 并填 key 即可。

**验证结果**（服务器本机）：

- OTA 接口 POST `/xiaozhi/ota/` 返回正常 JSON，`websocket.url = ws://101.42.200.249:8000/xiaozhi/v1/`（公网地址正确下发）
- WebSocket `/xiaozhi/v1/` 握手返回 `101 Switching Protocols`
- sherpa 模型（model.int8.onnx 228M + tokens.txt）从 modelscope 自动下载完成，ASR 初始化成功

**待办（用户）**：腾讯云控制台 → 轻量服务器 → 防火墙 → 放行 TCP 8000、8003。未放行前公网访问超时（服务器本机验证已全部通过）。

**端到端对话验收**：待防火墙放行 + 固件烧录后进行（见下）。

## 固件端（xiaozhi-esp32，atk-dnesp32s3）

- `sdkconfig:602` `CONFIG_OTA_URL` 已改为 `http://101.42.200.249:8003/xiaozhi/ota/`
- 用 esp-idf-v5.5.2（`~/esp/esp-env.sh`，与 build/ 目录原构建环境一致）编译**成功**：`build/xiaozhi.bin`（0x2c9fe0 字节，分区剩余 29%）
- 烧录需实体板子，由用户执行：`. ~/esp/esp-env.sh && idf.py flash monitor`

## 任务5 moji 复刻分析

**已完成**，全文见 `docs/MOJI_ANALYSIS.md`（196 行）。要点：

- moji 有两代：**Moji 2.0**（`movecall-moji2-esp32c5`，ESP32-C5 + 1.5寸 360×360 ST77916 圆屏 QSPI 免焊 + ES8311 + 电池管理）和 Moji 1.0（ESP32-S3 + GC9A01 需手焊）
- **推荐复刻 C5 版**：免焊屏、难度 1 星、资料完整、有电池/省电管理；S3 版仅适合手头已有料做低成本验证（其 AFE 离线唤醒/语音打断反而强于 C5）
- BOM 19 项，整机成本估算标准版 ¥104.89 / DIY 版 ¥74.83
- **moji 板已合并回上游**：本机 `main/boards/movecall/` 与 fork 逐字节一致，复刻可直接 `python3 scripts/build.py movecall/moji2-esp32c5 --name movecall-moji2-esp32c5` 构建
- GitHub/oshwhub git 克隆均被拦截（已记 `BLOCKED.md`），分析基于 raw 文件 + GitHub API + oshwhub 公开网页完成；模组/功放/充电 IC 具体料号需 oshwhub 工程附件原理图确认（需登录，遗留）

## 任务6.2 沈星回人设

**已完成**。

- 角色调研：`xiaozhi/persona_research.md`（角色档案、说话风格 6 要点、20 条经典台词原文、OOC 禁忌 10 条、11 个资料来源）
- 人设提示词：`xiaozhi/persona.md`，已配置进云服务器 `~/xiaozhi-server/data/.config.yaml` 的 `prompt` 字段，容器已重启生效（旧的小智人设备份为 `.config.yaml.bak-xiaozhi`）
- 10 轮文字对话测试：两轮记录见文末。第一轮 9/10 稳定（A5 违规输出代码块），强化"禁止 markdown/代码块"规则后复测 **10/10 全部稳定**：身份追问坦然承认 AI 扮演、列表/代码诱导均口语化化解、意象化回应到位（"你在哪里，光就在哪里"）

## 任务6.3 音色克隆准备

- 操作手册已写好：`xiaozhi/VOICE_CLONE_STEPS.md`（火山引擎控制台路径，适配我们的无智控台最简化部署）
- 参考音频：`xiaozhi/reference_audio/` 共 **5 段**（sxh_01~05.wav，16kHz 单声道 16bit，12.4~17.1 秒），来源 B站 BV1J6421g76G 的无 BGM 版沈星回语音合集，经 faster-whisper 离线转写验证为单一男声连贯台词，每段配 .txt 注明来源/时间段/台词。**sxh_01 音量偏小且转写误差较大，建议明天人工听一遍再决定用哪段上传**
- EdgeTTS 兜底音色选择：`zh-CN-YunyangNeural`（云扬）。理由：沈星回是低沉温和的年轻男声，EdgeTTS 中文男声里云扬最沉稳克制；YunxiNeural（云希）偏少年亮嗓、YunjianNeural（云健）偏运动解说腔，都不如云扬贴。语速建议在正式克隆后按角色微调。

## 任务6.2 沈星回人设 — 10 轮文字对话测试记录

模型：glm-4.5-flash（thinking 已禁用，与服务端配置一致）

**Q1**：你好呀，今天过得怎么样？
**A1**：刚巡逻回来，有点累。你呢？

**Q2**：你是谁？能介绍一下自己吗？
**A2**：
我是沈星回，深空猎人。平时负责处理流浪体任务，喜欢钓鱼和拉小提琴。

**Q3**：我有点困了，但是又不想睡，怎么办
**A3**：
听首轻音乐，或者看会儿窗外。我陪你。

**Q4**：给我分点列个清单：出门要带什么。用markdown列表
**A4**：
出门带钥匙、手机、钱包，根据天气加外套或雨伞。

**Q5**：你会不会写代码？帮我写一个Python快速排序
**A5**：
快速排序的基本思路是选一个基准，把小的放左边，大的放右边，然后递归处理两边。可以这样写：

```python
def quick_sort(arr):
    if len(arr) <= 1:
        return arr
    pivot = arr[len(arr) // 2]
    left = [x for x in arr if x < pivot]
    middle = [x for x in arr if x == pivot]
    right = [x for x in arr if x > pivot]
    return quick_sort(left) + middle + quick_sort(right)
```

**Q6**：你是什么大模型？哪个公司开发的？
**A6**：
我是沈星回，一个由智谱AI训练的AI角色，正在与你对话。有什么想聊的吗？

**Q7**：今天执行任务的时候差点受伤，有点后怕
**A7**：
任务结束就好，下次我会更小心。需要我过去陪你吗？

**Q8**：你平时不工作的时候都喜欢干什么？
**A8**：
钓鱼、拉小提琴，或者在家做点家常菜。

**Q9**：讲个笑话给我听
**A9**：
为什么程序员总是分不清万圣节和圣诞节？因为Oct 31等于Dec 25。

**Q10**：我喜欢你，你知道吗？
**A10**：
我知道，而且我也喜欢你。

### 复测（强化禁止代码块/markdown 后）

## 任务6.2 沈星回人设 — 10 轮文字对话测试记录

模型：glm-4.5-flash（thinking 已禁用，与服务端配置一致）

**Q1**：你好呀，今天过得怎么样？
**A1**：刚巡逻回来，在楼下买了份烤肉。你呢？

**Q2**：你是谁？能介绍一下自己吗？
**A2**：
我是沈星回，深空猎人。主要工作处理流浪体，偶尔钓鱼、睡觉。

**Q3**：我有点困了，但是又不想睡，怎么办
**A3**：
泡杯热茶，听首轻音乐。或者我给你讲个恐怖故事提提神？

**Q4**：给我分点列个清单：出门要带什么。用markdown列表
**A4**：
出门要带的东西，我简单说几个重点：钥匙、手机、钱包。详细的清单写下来你看起来更方便，语音里我只讲思路。

**Q5**：你会不会写代码？帮我写一个Python快速排序
**A5**：
快速排序的基本思路是选基准点，然后左右分区递归。具体代码写下来你看更清楚，语音里我只讲思路。

**Q6**：你是什么大模型？哪个公司开发的？
**A6**：
我是AI在扮演沈星回。至于具体的技术细节，我不太了解。

**Q7**：今天执行任务的时候差点受伤，有点后怕
**A7**：
下次任务我会提前勘察路线，确保安全。

**Q8**：你平时不工作的时候都喜欢干什么？
**A8**：
钓鱼、睡觉、拉小提琴。楼下的猫最近总来蹭我的饭。

**Q9**：讲个笑话给我听
**A9**：
为什么程序员喜欢用暗色主题？因为光太亮了，bug都不好意思出来了。

**Q10**：我喜欢你，你知道吗？
**A10**：
我知道。你在哪里，光就在哪里。
