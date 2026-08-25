# 深夜电台 —— 最小可开关版本

> 状态：**内容生成与定时调度已完成（本目录）**；推送到设备播放依赖架构决策，见 `../../待确认清单.md` 第 1 条。
> 开关：本目录下 `ENABLED` 文件存在即启用（`touch ENABLED` 开 / `rm ENABLED` 关），cron 每分钟检查一次。

## 组成

- `gen_radio_segment.py`：用 GLM（沈星回人设）生成一段 2~3 句的"深夜电台"稿子，再用火山克隆音色合成 mp3，存 `out/电台_YYYYMMDD_HHMM.mp3`
- `radio_cron.sh`：cron 入口，检查 ENABLED 开关和时段（默认 23:00-23:59），调用生成脚本
- `out/`：生成的音频存档（可直接点开听；推送到桌宠播放是待确认项）

## 安装（crontab 示例）

```cron
* 23 * * * /home/applejennie/esp/project/radio/radio_cron.sh >> /home/applejennie/esp/project/radio/cron.log 2>&1
```

## 依赖

- GLM key（环境变量 `ZHIPU_API_KEY`）
- 火山凭据（`HUOSHAN_APPID` / `HUOSHAN_TOKEN`）
- ffmpeg（~/.overnight-tools 里的静态二进制，可选，仅用于转 wav）
