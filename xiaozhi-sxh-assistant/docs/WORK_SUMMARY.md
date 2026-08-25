# 云服务器部署与沈星回角色化 — 工作总结

> 写于 2026-08-25。记录"把小智服务端迁上腾讯云 + 角色化为沈星回"的完整过程和结论。
> 过程性日志在 `../../project/OVERNIGHT_LOG.md`，本文是结论性总结。

## 1. 最终架构

```
ESP32 开发板（atk-dnesp32s3，固件 v2.4.2，与上游逐字节一致）
   │  唤醒词"你好小智" → Opus 音频
   ▼  WebSocket  ws://101.42.200.249:8000/xiaozhi/v1/
腾讯云轻量服务器（101.42.200.249，2核2G，Ubuntu 22.04，Docker 最简化部署）
   ├─ VAD   SileroVAD（镜像内置）
   ├─ ASR   SherpaASR（sherpa-onnx sense_voice int8，228M，本地小模型）
   ├─ LLM   智谱 glm-4.5-flash（免费，沈星回人设 prompt）
   ├─ TTS   火山引擎双流式 TTS（沈星回克隆音色；EdgeTTS 云扬为兜底）
   └─ 记忆/意图  mem_local_short / function_call
```

资源占用：容器限 1.2G（实测约 745MB），系统 swap 3G，容器 `restart: always`。

## 2. 关键决策与理由

- **ASR 没用任务书要求的云端 API**：火山 ASR 需要账号开通（用户操作），为跑通端到端先用 sherpa-onnx int8 本地小模型（非 FunASR 的 torch 大模型，2核2G 可承受）。火山 key 到手后改 `.config.yaml` 的 `selected_module.ASR` 即可切换
- **人设经过两轮 10 轮测试**：首轮发现代码诱导题会输出代码块，强化"禁止 markdown/代码块"规则后复测 10/10 稳定。人设源文件 `project/xiaozhi/persona.md`，调研素材 `persona_research.md`
- **音色走火山克隆**：素材为 B站无 BGM 版沈星回语音合集剪出的 5 段 16kHz WAV（faster-whisper 离线转写校验），操作手册 `project/xiaozhi/VOICE_CLONE_STEPS.md`

## 3. 踩坑记录（下次省事）

1. **腾讯云轻量服务器的防火墙在控制台，不在系统里**——8000/8003 必须在网页控制台放行，ufw 里看不到
2. **固件 OTA 地址有 NVS 缓存**：`Ota::GetCheckVersionUrl()`（main/ota.cc:46）优先读 NVS `wifi` 命名空间的 `ota_url`，改 sdkconfig 重编译**不够**，必须擦 NVS（`esptool erase_region 0x9000 0x4000`）或专门清 key，否则设备仍连旧服务器
3. **VMware 虚拟机认不到开发板**：USB 串口要在"虚拟机 → 可移动设备"里手动透传，出现 `/dev/ttyUSB0` 才能烧录
4. **ESP-IDF 环境**：本工程 build/ 用 esp-idf-v5.5.2（`~/esp/esp-env.sh`），不是 AGENTS.md 首选的 v6.0.2
5. **密码里的反引号**：SSH 脚本内嵌密码时，双引号不保护反引号（会触发命令替换），必须用单引号或脚本文件传输
6. **板子温热属正常**：AFE 唤醒检测全时运行是主要热源，40~55°C 手感正常；异常发烫再考虑散热片/降频

## 4. 服务器运维速查

```bash
ssh ubuntu@101.42.200.249
cd ~/xiaozhi-server
sudo docker compose restart          # 改完 data/.config.yaml 后重启生效
sudo docker logs -f xiaozhi-esp32-server   # 看全链路日志（ASR/LLM/TTS）
sudo docker stats --no-stream        # 内存占用
```

配置备份：`data/.config.yaml.bak-xiaozhi`（原小智人设）。切回 EdgeTTS 兜底音色：`selected_module.TTS` 改回 `EdgeTTS` 重启即可。

## 5. 遗留事项

- [ ] 天气插件需单独 API key（问天气会报认证失败，且因"插件失败 + 二次 LLM 圆场"要 ~43 秒；注册和风天气 dev.qweather.com 免费 key 配上即可秒回，或摘除 get_weather 插件）
- [ ] 服务器登录密码已在对话中暴露，待控制台重置；火山 Access Token / API Key 注意保密
- [x] ~~音色相似度优化~~ → 已于 08-25 晚完成（见第 7 节）
- [ ] moji 复刻（见 `../../project/docs/MOJI_ANALYSIS.md`）：推荐 C5 版，BOM 约 ¥105，板型已在上游 `main/boards/movecall/`，可直接 build.py 构建
- [ ] sxh_01 参考音频音量偏小，人工校对后可决定是否弃用

## 7. 音色升级攻坚战（08-25 晚，已通过验收）

**结果**：音色从"12 秒素材训练的 ICL1.0"升级为"112 秒婚卡台词训练的 ICL2.0（model_type=5）"，用户板子验收通过。

**最终生效配置**（`~/xiaozhi-server/data/.config.yaml`，旧配置已注释备份在同文件内）：

| 项 | 值 |
|---|---|
| appid / access_token | 新应用 `3739237418`（旧应用 2215079862 弃用） |
| resource_id | `seed-icl-2.0` |
| speaker | `S_DX79g48d2`（付费音色槽位，剩余训练次数 13） |
| speech_rate | -8（沈星回语速偏慢） |

**素材处理**：桌面 `沈星回.mp3` 实为手机录屏视频（HEVC + AAC 44.1kHz，1:58），抽音轨 → silencedetect 切分 → faster-whisper 转写逐句校验（确认是婚卡台词）→ 剔除开头 6 秒他人声 → 输出 112 秒 24kHz 单声道 WAV（5.4MB）+ 55 秒备用版（均在桌面）。训练参数：`enable_crop_by_asr=true`（防切字）、`voice_clone_enable_mss=true`（音源分离去 BGM）。

**踩坑记录（本次最有价值的部分）**：

1. **声音复刻的权限是三层分离的**：① 控制台界面克隆（买应用时送的流程）② 训练 API（`volc.megatts.timbre`）③ 合成 API（`volc.seedicl.default` 等）——各自独立授权，报错全是 45000030 not granted / 55000000 resource mismatched。控制台能克隆 ≠ API 能训练 ≠ API 能合成。
2. **可行路径（已验证）**：控制台购买 SeedICL 预付费音色槽位（得 `S_` 开头 ID）→ 新版控制台 API Key（X-Api-Key）调 `POST /api/v3/tts/voice_clone` 训练 → 合成时小智服务端用**旧版鉴权**（appid + access_token，`X-Api-App-Key/X-Api-Access-Key`）+ `resource_id: seed-icl-2.0`。xiaozhi 的 huoshan_double_stream provider 只支持旧版鉴权头。
3. **V3 训练一次得两个效果**：返回 model_type 1（ICL1.0）和 5（ICL V3），合成时分别对应 resource_id `seed-icl-1.0` / `seed-icl-2.0`，2.0 效果更好。
4. **unidirectional 与 bidirection 端点授权独立**：单向 HTTP 端点 403 不代表线上双向流式不通——旧应用曾"API 测试全 403 但设备照常说话"，虚惊一场，诊断时别被误导。
5. **我自己的失误**：`restart_server.sh` 只重启不拷贝 `/tmp/xz_upload/.config.yaml`，导致语速调整和新音色切换两次"假生效"（用户报"音色没变"才暴露）。已修复：脚本改为 cp + 备份 + 容器内 grep 验证。**改配置必须验证容器内文件，不能只看重启成功**。
6. 火山新旧控制台双轨：新控制台用 X-Api-Key 单钥，旧控制台用 appid+token；资源按项目隔离。

**试听文件（桌面）**：`沈星回_新音色试听.mp3`（官方 demo）、`沈星回_新音色_合成测试_icl10.mp3` / `_icl20.mp3`（同一句话两种效果对比）。

## 6. 相关文件索引

| 文件 | 内容 |
|---|---|
| `../../project/OVERNIGHT_LOG.md` | 全过程日志 + 两轮 10 轮人设测试记录 |
| `../../project/docs/MOJI_ANALYSIS.md` | moji 复刻分析（硬件方案/BOM/C5 vs S3） |
| `../../project/xiaozhi/persona.md` | 沈星回人设（服务端 prompt 的源文件） |
| `../../project/xiaozhi/persona_research.md` | 角色调研（台词原文/OOC 禁忌/资料来源） |
| `../../project/xiaozhi/persona_test.py` | 人设 10 轮测试脚本（**含 API key，勿外传**） |
| `../../project/xiaozhi/VOICE_CLONE_STEPS.md` | 火山克隆操作手册 |
| `../../project/xiaozhi/reference_audio/` | 克隆参考音频（**版权素材，仅限个人使用，勿公开分发**） |
| `../../project/BLOCKED.md` | 受阻项记录（oshwhub 克隆、防火墙） |
