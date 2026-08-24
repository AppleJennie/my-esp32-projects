# BLOCKED 记录

## 2026-08-24 MoveCall/xiaozhi-esp32 GitHub 克隆失败

- `git clone --depth 1 https://github.com/MoveCall/xiaozhi-esp32` → 失败：Failed to connect to github.com port 443: Connection refused
- 镜像 `https://gitclone.com/github.com/MoveCall/xiaozhi-esp32` → 失败：HTTP 504
- 镜像 `https://ghproxy.com/https://github.com/MoveCall/xiaozhi-esp32` → 失败：连接 ghproxy.com:443 超时

已按预案改为通过 raw.githubusercontent.com / 网页抓取进行分析。

## 2026-08-24 oshwhub git 克隆失败（次要目标）

- `git clone --depth 1 https://oshwhub.com/movecall/moji2.git` → 失败：repository not found（oshwhub git 需登录）
- 但 oshwhub 项目网页 https://oshwhub.com/movecall/moji2 可公开访问，已用网页内容补全硬件细节（BOM、结构、FAQ）。

## 公网访问 8000/8003 被腾讯云防火墙拦截（2026-08-25 00:52）

- 现象：从外网 curl `http://101.42.200.249:8003/xiaozhi/ota/`（POST）和 `ws://101.42.200.249:8000/xiaozhi/v1/`（Upgrade 握手）均连接超时；端口 22 (SSH) 正常
- 服务器本机验证全部通过：OTA POST 返回正常 JSON（含公网 websocket.url）、WS 握手返回 101 Switching Protocols、`ss -tlnp` 确认 0.0.0.0:8000/8003 在监听
- 结论：服务本身无问题，唯一卡点是腾讯云轻量服务器的**控制台防火墙**未放行 TCP 8000/8003
- 需要的操作（只能用户做，Agent 无控制台权限）：腾讯云控制台 → 轻量应用服务器 → 实例详情 → 防火墙 → 添加规则：TCP 8000、TCP 8003
- 放行后验证命令（本机执行）：
  curl -X POST http://101.42.200.249:8003/xiaozhi/ota/ -H 'Content-Type: application/json' -H 'Device-Id: 11:22:33:44:55:66' -H 'Client-Id: t1' -d '{"application":{"version":"2.4.2"},"board":{"type":"atk-dnesp32s3"}}'
  应返回含 "websocket":{"url":"ws://101.42.200.249:8000/xiaozhi/v1/"} 的 JSON
