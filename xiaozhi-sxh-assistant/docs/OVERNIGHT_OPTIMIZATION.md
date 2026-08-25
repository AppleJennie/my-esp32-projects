# 通宵优化与回归体系（2026-08-26 凌晨）

> 通宵任务书执行记录。原则：线上零破坏（配置先备份、改完冒烟、凭据只读、架构决策进待确认）。

## 1. 回归测试体系（核心产出）

`~/esp/project/regression/`（仓库脱敏版 `tools/regression/`）：

- `gen_clips.py` — 用火山 TTS 合成三个测试问题（你是谁/讲故事/查天气），整段编码 Ogg/Opus 后按 Ogg 页解析拆出 60ms 裸 opus 帧（**坑：ffmpeg `-f opus` 输出的是 Ogg 容器不是裸帧，服务端 opuslib_next 直接丢弃，日志只有 DEBUG**）
- `regression_test.py` — 模拟 ESP32 设备的 WS 客户端：hello → listen start → 3倍速发帧 → stop，断言 ASR 文本、TTS 音频下行（>5KB）、线上配置红线（ssh 检查 speaker 槽位 ID + prompt 沈星回关键字）。GLM 免费档限流，用例间隔 45s + 每例最多 3 次重试
- 用法：`PYTHONPATH=~/.overnight-tools/pylibs python3 regression_test.py`，退出码 0=全过
- 注意点：①取**第一条** stt 消息（function_call 后续会再发工具调用文本如 `% get_weather`）；②双流式 TTS 的 sentence_start 文本为空，断言看音频不看文本；③llm 表情消息不是每轮都有，用音频下行证明 LLM 有响应

**基线延迟**（三次全量 PASS 的统计）：ASR 结果 ≈0.1s；首个音频下行 1.1~3.6s（短回复）、6~7s（长回复如讲故事）；整轮 6~15s。

## 2. 服务器稳定性

- compose 增加 healthcheck：每 60s 探测 OTA HTTP 接口，start_period 120s（`docker ps` 已显示 healthy）；`restart: always` 原有保留；unhealthy 自动重启（autoheal）进待确认清单
- docker 服务 systemctl enabled ✓（开机自启）
- 备份：`~/xiaozhi-server/docker-compose.yml.bak-20260825`
- ESP32 端重连核查（只读）：WiFiManager 断线自动重扫重连（递增退避 10s→20s→40s），激活/OTA 失败指数退避（application.cc:433-479），WS 断开后下次唤醒重新握手——逻辑完备，无需改动

## 3. 响应速度优化

- **天气插件从 function_call 移除**（配置备份 `.bak-*` 同目录）：消除了"插件认证失败 + 二次 LLM 圆场"的 43 秒惩罚，问天气现在由人设直接回答（15s 内）。key 到手后 functions 加回 `get_weather` + `plugins.get_weather` 填 key 即可（模板已在配置和仓库）
- 其余链路（sherpa ASR 0.1s、GLM 流式、火山双流 TTS）已是合理水平，未做风险改动

## 4. 深夜电台（最小可开关版）

`~/esp/project/radio/`：GLM 人设写稿（2-3 句晚安电台）→ 火山克隆音色合成 mp3（speech_rate -8）→ `out/` 存档；`ENABLED` 文件开关 + cron 时段控制（默认 23 点档）。已实测生成一次成功。
**推送到设备播放**需要 MQTT 网关或固件改造（架构决策）→ `~/esp/project/待确认清单.md` 第 1 条。

## 5. 闹钟/定时提醒

与电台同卡点（服务端无法触达空闲设备）。方案已写进待确认清单第 2 条（MQTT 网关 / 设备端 MCP 闹钟工具两路）。

## 6. 遗留与待确认

全部见 `~/esp/project/待确认清单.md`：电台/闹钟推送方案、autoheal、GLM 限流（429）、天气 key、凭据重置。
