#!/usr/bin/env python3
"""小智云端回归测试：模拟 ESP32 设备走完整 WebSocket 协议，
对三个问题分别验证 ASR -> LLM -> TTS 全链路，输出延迟与断言报告。

用法: PYTHONPATH=~/.overnight-tools/pylibs python3 regression_test.py [--ws ws://host:8000/xiaozhi/v1/]
退出码: 全部通过 0，任一失败 1。
"""
import asyncio
# export XZ_SSH_PASS=... 后运行; 依赖 websockets 库
import glob
import json
import os
import subprocess
import sys
import time

import websockets

HERE = os.path.dirname(os.path.abspath(__file__))
WS_URL = "ws://101.42.200.249:8000/xiaozhi/v1/"
SERVER = "ubuntu@101.42.200.249"
SSH_PASS = os.environ["XZ_SSH_PASS"]  # export XZ_SSH_PASS=服务器密码
EXPECT_SPEAKER = os.environ.get("EXPECT_SPEAKER", "<音色槽位ID>")

CASES = [
    # (名称, 问题, ASR断言模式: exact=完全匹配 / contains=包含子串)
    ("who", "你是谁？", ("exact", "你是谁")),
    ("story", "给我讲个故事吧。", ("contains", "故事")),
    ("weather", "帮我查一下天气。", ("contains", "天气")),
]

HELLO = {
    "type": "hello", "version": 1, "transport": "websocket",
    "audio_params": {"format": "opus", "sample_rate": 16000,
                     "channels": 1, "frame_duration": 60},
}


def check_server_config():
    """ssh 到服务器确认线上音色与人设配置未被改动（红线检查）"""
    try:
        out = subprocess.run(
            ["sshpass", "-p", SSH_PASS, "ssh", "-o", "StrictHostKeyChecking=no",
             SERVER, "grep -c '沈星回' ~/xiaozhi-server/data/.config.yaml; "
             "grep -m1 'speaker:' ~/xiaozhi-server/data/.config.yaml"],
            capture_output=True, text=True, timeout=30)
        lines = out.stdout.strip().splitlines()
        persona_ok = lines and lines[0].isdigit() and int(lines[0]) >= 3
        speaker_ok = any(EXPECT_SPEAKER in l for l in lines)
        return persona_ok and speaker_ok, \
            f"prompt沈星回x{lines[0] if lines else '?'}, {lines[-1].strip()}"
    except Exception as e:
        return False, f"ssh 检查失败: {e}"


async def run_case(name, question, expect_keywords):
    frames = sorted(glob.glob(f"{HERE}/clips/{name}_frames/*.opus"))
    assert frames, f"缺少 {name} 的音频帧，先跑 gen_clips.py"
    result = {"case": name, "question": question, "frames": len(frames)}

    headers = {"Device-Id": "aa:bb:cc:dd:ee:ff", "Client-Id": "regression-test-001"}
    try:
        ws = await websockets.connect(WS_URL, additional_headers=headers,
                                      ping_interval=None, close_timeout=5)
    except TypeError:  # 老版本 websockets
        ws = await websockets.connect(WS_URL, extra_headers=headers,
                                      ping_interval=None, close_timeout=5)

    stt_text, tts_texts = "", []
    audio_bytes = 0
    llm_seen = False
    t_start = time.time()
    t_first_audio = None
    t_stt = None
    try:
        await ws.send(json.dumps(HELLO))
        # 等服务端 hello
        while True:
            msg = json.loads(await asyncio.wait_for(ws.recv(), 10))
            if msg.get("type") == "hello":
                break
        await ws.send(json.dumps({"type": "listen", "state": "start", "mode": "auto"}))
        # 按近似实时速度发音频帧
        for fp in frames:
            with open(fp, "rb") as f:
                await ws.send(f.read())
            await asyncio.sleep(0.02)  # 3 倍速，兼顾 VAD 与速度
        t_sent = time.time()
        await ws.send(json.dumps({"type": "listen", "state": "stop"}))

        # 收消息直到 tts stop 或超时
        while True:
            try:
                raw = await asyncio.wait_for(ws.recv(), 60)
            except asyncio.TimeoutError:
                result["error"] = "等待服务端响应超时(60s)"
                break
            if isinstance(raw, bytes):
                audio_bytes += len(raw)
                if t_first_audio is None:
                    t_first_audio = time.time()
                continue
            msg = json.loads(raw)
            mtype = msg.get("type")
            if mtype == "stt":
                # 只取第一条 stt（真实 ASR 结果）；function_call 后续可能再发工具调用文本
                if not stt_text:
                    stt_text = msg.get("text", "")
                    t_stt = time.time()
            elif mtype == "llm":
                llm_seen = True
            elif mtype == "tts":
                state = msg.get("state")
                # 双流式 TTS 的 sentence_start 文本为空，只记录非空文本
                if state == "sentence_start" and msg.get("text"):
                    tts_texts.append(msg["text"])
                elif state == "stop":
                    break
    finally:
        await ws.close()

    # 断言
    mode, expect = expect_keywords
    asr_clean = stt_text.rstrip("。？?，,")
    if mode == "exact":
        asr_ok = asr_clean == expect
    else:
        asr_ok = expect in stt_text
    checks = []
    checks.append((f"ASR 识别(期望{mode}《{expect}》, 实得《{stt_text}》)", asr_ok))
    # 有 TTS 音频下行即证明 LLM 产出了文本（llm 表情消息并非每轮都有）
    checks.append(("LLM→TTS 有音频下行(>5KB)", audio_bytes > 5120))
    result.update({
        "stt": stt_text,
        "tts_reply": "".join(tts_texts)[:120],
        "audio_kb": round(audio_bytes / 1024, 1),
        "latency": {
            "发送完→ASR结果": round((t_stt - t_sent), 2) if t_stt else None,
            "发送完→首个音频": round((t_first_audio - t_sent), 2) if t_first_audio else None,
            "总耗时": round(time.time() - t_start, 2),
        },
        "checks": checks,
        "pass": all(ok for _, ok in checks),
    })
    return result


async def amain():
    report = {"ws": WS_URL, "time": time.strftime("%Y-%m-%d %H:%M:%S"), "cases": []}
    ok, detail = await asyncio.get_event_loop().run_in_executor(None, check_server_config)
    report["server_config"] = {"expect_speaker": EXPECT_SPEAKER, "ok": ok, "detail": detail}
    for i, (name, q, kw) in enumerate(CASES):
        if i > 0:
            await asyncio.sleep(45)  # GLM 免费档限流，用例间留间隔
        r = None
        for attempt in range(3):
            try:
                r = await run_case(name, q, kw)
            except Exception as e:
                r = {"case": name, "question": q, "pass": False,
                     "error": f"{type(e).__name__}: {e}"}
            if r.get("pass"):
                break
            if attempt < 2:
                print(f"[RETRY{attempt + 1}] {name} 未过（{r.get('error', '断言失败')}），25 秒后重试…")
                await asyncio.sleep(25)
        report["cases"].append(r)
        status = "PASS" if r.get("pass") else "FAIL"
        print(f"[{status}] {name}: {r.get('question')}")
        print(f"   ASR: {r.get('stt', r.get('error', ''))}")
        print(f"   音频: {r.get('audio_kb', 0)}KB  延迟: {r.get('latency')}")
        for cname, cok in r.get("checks", []):
            print(f"   {'✓' if cok else '✗'} {cname}")
    report["config_ok"] = report["server_config"]["ok"]
    report["all_pass"] = report["config_ok"] and all(c.get("pass") for c in report["cases"])
    out = f"{HERE}/last_report.json"
    with open(out, "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=2)
    print(f"\n线上配置(音色+人设): {'OK ' + detail if ok else '异常! ' + detail}")
    print(f"总体: {'ALL_PASS' if report['all_pass'] else 'FAILED'} (报告: {out})")
    return 0 if report["all_pass"] else 1


if __name__ == "__main__":
    sys.exit(asyncio.get_event_loop().run_until_complete(amain()))
