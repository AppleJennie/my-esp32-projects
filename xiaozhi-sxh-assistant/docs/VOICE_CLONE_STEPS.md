# 沈星回音色克隆操作手册（明天照做，约 10 分钟）

> 目标：把小智的 TTS 从 EdgeTTS（兜底）换成火山引擎克隆的沈星回音色。
> 前提：本目录 `reference_audio/` 里已准备好 3~5 段 10~20 秒干净语音 WAV（今晚 Agent 已尽量收集，若数量为 0 请先看 `../BLOCKED.md` 的替代方案）。
> 部署形态提醒：我们的云服务器是**最简化 Docker 部署（无智控台）**，所以克隆在**火山引擎官网控制台**完成，音色 ID 直接写进服务端配置文件。官方智控台路径见 `xiaozhi-esp32-server/docs/huoshan-streamTTS-voice-cloning.md`（如果以后升级全模块部署才用得上）。

## 第一步：开通火山引擎语音服务（约 5 分钟，需本人实名）

1. 打开 https://console.volcengine.com/speech/app ，登录（支持抖音/手机号），按提示完成**实名认证**。
2. 在「应用管理」创建一个应用，勾选两个服务：
   - **语音合成大模型**
   - **声音复刻大模型**
3. 默认会赠送一个音色复刻资源（一个可克隆名额）。想克隆多个音色才需要额外购买，一个就够我们用。

## 第二步：拿到三样东西

打开 https://console.volcengine.com/speech/service/9999 ，复制：

| 项目 | 长什么样 | 用途 |
|---|---|---|
| App ID | 一串数字 | 服务端配置 `appid` |
| Access Token | 一串字母数字 | 服务端配置 `access_token` |
| 声音资源 ID | `S_xxxxxxxx` | 克隆入口 / 最终音色 ID |

## 第三步：上传音频克隆音色（约 2 分钟）

1. 在「声音复刻」页面选择你的声音资源，点**上传音频**。
2. 从 `reference_audio/` 里挑**最干净的一段**（无 BGM、无音效、单人声、10~20 秒）上传。
3. 页面上可以试听、截取片段，确认后提交，状态变为「待复刻」。
4. 点**立即复刻**，几秒后显示「训练成功」。如果失败，换 `reference_audio/` 里另一段再试（失败原因多为底噪/BGM/时长不够）。

## 第四步：把克隆音色配进小智服务端（约 2 分钟）

SSH 到云服务器，编辑配置：

```bash
ssh ubuntu@101.42.200.249
nano ~/xiaozhi-server/data/.config.yaml
```

把 `selected_module` 里的 `TTS: EdgeTTS` 改为 `TTS: HuoshanDoubleStreamTTS`，并在 `TTS:` 段追加：

```yaml
TTS:
  HuoshanDoubleStreamTTS:
    type: huoshan_double_stream
    ws_url: wss://openspeech.bytedance.com/api/v3/tts/bidirection
    appid: <第二步的 App ID>
    access_token: <第二步的 Access Token>
    resource_id: volc.service_type.10029
    speaker: <克隆成功的音色ID，S_xxxxxxxx>
    audio_params:
      speech_rate: 0    # 语速 -50~100，沈星回语速偏慢可试 -5
      loudness_rate: 0
```

（EdgeTTS 段保留不删，想切回兜底音色时把 `selected_module.TTS` 改回 `EdgeTTS` 即可。）

重启容器生效：

```bash
cd ~/xiaozhi-server && sudo docker compose restart
```

## 第五步：验证

对设备（或测试页）说一句话，听到克隆音色回复即成功。听感不满意就回第三步换一段参考音频重新克隆（重新克隆会覆盖同一音色资源，服务端配置不用改）。

## 注意事项

- **费用**：语音合成大模型按调用量计费（有免费额度/新人包，以官网为准）；声音复刻名额默认送 1 个。记得只在测试阶段使用，避免长时间挂机跑量。
- **版权**：游戏角色音色仅可个人把玩，**不要商用、不要公开分发克隆出的音色或音频**。
- **备用**：如果火山开通失败，`selected_module.TTS` 保持 `EdgeTTS` 即可，当前配置的云扬（zh-CN-YunyangNeural）就是兜底音色，选择理由见 `../OVERNIGHT_LOG.md`。
