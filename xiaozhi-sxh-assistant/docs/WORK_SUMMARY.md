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

- [ ] 天气插件需单独 API key（问天气会报认证失败，其他功能不受影响）
- [ ] 服务器登录密码已在对话中暴露，待控制台重置；火山 Access Token 注意保密
- [ ] 音色相似度优化：可将 2~3 段参考音频拼成 ~40 秒长素材在火山控制台重新克隆
- [ ] moji 复刻（见 `../../project/docs/MOJI_ANALYSIS.md`）：推荐 C5 版，BOM 约 ¥105，板型已在上游 `main/boards/movecall/`，可直接 build.py 构建
- [ ] sxh_01 参考音频音量偏小，人工校对后可决定是否弃用

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
