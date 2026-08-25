#!/bin/bash
# 深夜电台 cron 入口：开关 + 时段检查 + 调用生成脚本
DIR="$(cd "$(dirname "$0")" && pwd)"
[ -f "$DIR/ENABLED" ] || exit 0   # 开关：无 ENABLED 文件直接退出
HOUR=$(date +%H)
[ "$HOUR" = "23" ] || exit 0       # 只在 23 点时段生成（时段可按需改）
# 同一小时内只生成一次
STAMP=$(date +%Y%m%d_%H)
ls "$DIR/out/电台_${STAMP}"*.mp3 >/dev/null 2>&1 && exit 0
export PYTHONPATH="$HOME/.overnight-tools/pylibs"
exec python3 "$DIR/gen_radio_segment.py"
