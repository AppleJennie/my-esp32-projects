#!/usr/bin/env python3
"""调用火山引擎声音复刻 V3 API 训练/查询音色"""
import base64
import json
import sys
import time
import uuid
import urllib.request

APPID = "<旧版控制台appid，新版用API Key见文件尾>"
TOKEN = "<access_token>"
SPEAKER = "S_DX79g48d2"  # 当前槽位

HEADERS = {
    "Content-Type": "application/json",
    "X-Api-App-Key": APPID,
    "X-Api-Access-Key": TOKEN,
}


def post(url, payload):
    headers = dict(HEADERS)
    headers["X-Api-Request-Id"] = str(uuid.uuid4())
    req = urllib.request.Request(
        url, data=json.dumps(payload).encode(), headers=headers, method="POST"
    )
    try:
        with urllib.request.urlopen(req, timeout=120) as r:
            return r.status, json.load(r)
    except urllib.error.HTTPError as e:
        body = e.read().decode(errors="replace")
        try:
            return e.code, json.loads(body)
        except Exception:
            return e.code, {"raw": body}


def train(wav_path):
    with open(wav_path, "rb") as f:
        data = base64.b64encode(f.read()).decode()
    payload = {
        "speaker_id": SPEAKER,
        "audio": {"data": data, "format": "wav"},
        "language": 0,
        "extra_params": {
            "enable_crop_by_asr": True,      # ASR 截断，避免切字
            "voice_clone_enable_mss": True,  # 音源分离去背景音（录屏素材保险）
        },
        "demo_text": "无论多少次，无论你在哪，我都会找到你。需要后援时，我一直都在。",
    }
    print(f"上传 {len(data)/1024/1024:.1f}MB(base64) 到 voice_clone ...", flush=True)
    return post("https://openspeech.bytedance.com/api/v3/tts/voice_clone", payload)


def status():
    return post(
        "https://openspeech.bytedance.com/api/v3/tts/get_voice",
        {"speaker_id": SPEAKER},
    )


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "train":
        code, resp = train(sys.argv[2])
        print("HTTP", code)
        print(json.dumps(resp, ensure_ascii=False, indent=2))
    elif cmd == "status":
        code, resp = status()
        print("HTTP", code)
        print(json.dumps(resp, ensure_ascii=False, indent=2))
