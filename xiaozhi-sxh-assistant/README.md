# my-esp32-project

ESP32 小智 AI 语音助手的个人项目资料：云端部署方案 + 《恋与深空》沈星回角色化（人设 prompt + 音色克隆）+ moji 复刻分析。

## 架构

```
ESP32 开发板（atk-dnesp32s3，固件 xiaozhi-esp32 v2.4.2）
   │  WebSocket
   ▼
腾讯云轻量服务器（2核2G，Docker 最简化部署 xiaozhi-esp32-server）
   ASR: sherpa-onnx int8（本地小模型）
   LLM: 智谱 glm-4.5-flash（沈星回人设）
   TTS: 火山引擎克隆音色（EdgeTTS 云扬兜底）
```

## 目录

- `docs/WORK_SUMMARY.md` — 工作总结：架构、决策理由、踩坑记录（NVS OTA 缓存、腾讯云防火墙等）、运维速查
- `docs/OVERNIGHT_LOG.md` — 过程日志 + 两轮 10 轮人设测试记录
- `docs/MOJI_ANALYSIS.md` — moji 桌面机器人复刻分析（硬件方案 / BOM / C5 vs S3）
- `docs/persona_research.md` — 沈星回角色调研（台词原文、OOC 禁忌、资料来源）
- `docs/VOICE_CLONE_STEPS.md` — 火山引擎音色克隆操作手册
- `docs/BLOCKED.md` — 受阻项记录
- `persona/persona.md` — 沈星回人设 prompt（服务端 `prompt` 字段的源文件）
- `deploy/` — 云服务器部署配置（docker-compose.yml + 脱敏的配置模板 .config.example.yaml）
- `tools/persona_test.py` — 人设 10 轮对话稳定性测试脚本（需 `export ZHIPU_API_KEY=...`）

## 说明

- 固件本体是上游 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)，未修改代码，个性化全在配置层
- 服务端是 [xinnan-tech/xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server) Docker 最简化部署
- 克隆音色涉及的音频素材（游戏角色语音）仅限个人使用，未收录进本仓库，也请勿公开分发
