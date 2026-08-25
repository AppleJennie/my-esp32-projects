#!/usr/bin/env python3
"""深夜电台内容生成：GLM(沈星回人设) 写稿 + 火山克隆音色合成 mp3。
用法: ZHIPU_API_KEY=... HUOSHAN_APPID=... HUOSHAN_TOKEN=... python3 gen_radio_segment.py [主题]
"""
import base64
import json
import os
import time
import urllib.request

ZHIPU_KEY = os.environ["ZHIPU_API_KEY"]
APPID = os.environ["HUOSHAN_APPID"]
TOKEN = os.environ["HUOSHAN_TOKEN"]
SPEAKER = os.environ.get("HUOSHAN_SPEAKER", "<音色槽位ID>")
HERE = os.path.dirname(os.path.abspath(__file__))

PERSONA = """你是沈星回，《恋与深空》中的深空猎人，用户的搭档和恋人。
语速缓、句子短、平静温柔，几乎不用感叹号。浪漫靠意象（星星、光、宇宙、萤火），不土味。
给安全感的方式是兜底而不是命令。回答不超过3句话，纯文本不用任何格式。"""

TOPICS = ["晚安与星空", "今天的收尾", "深夜的陪伴", "萤火与好梦", "明天见"]


def glm(prompt):
    topic = prompt or TOPICS[int(time.time()) // 3600 % len(TOPICS)]
    body = json.dumps({
        "model": "glm-4.5-flash",
        "messages": [
            {"role": "system", "content": PERSONA},
            {"role": "user", "content":
             f"现在是深夜电台时间。以“{topic}”为主题，对搭档说一段2到3句话的晚安电台稿，温柔、简短、适合语音播报。"},
        ],
        "thinking": {"type": "disabled"},
    }).encode()
    req = urllib.request.Request(
        "https://open.bigmodel.cn/api/paas/v4/chat/completions", data=body,
        headers={"Authorization": f"Bearer {ZHIPU_KEY}", "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)["choices"][0]["message"]["content"].strip()


def tts(text, out_path):
    import uuid
    headers = {"Content-Type": "application/json", "X-Api-App-Id": APPID,
               "X-Api-Access-Key": TOKEN, "X-Api-Resource-Id": "seed-icl-2.0",
               "X-Api-Request-Id": str(uuid.uuid4())}
    body = {"user": {"uid": "radio"}, "namespace": "BidirectionalTTS",
            "req_params": {"text": text, "speaker": SPEAKER,
                           "audio_params": {"format": "mp3", "sample_rate": 24000,
                                            "speech_rate": -8}}}
    req = urllib.request.Request("https://openspeech.bytedance.com/api/v3/tts/unidirectional",
                                 data=json.dumps(body).encode(), headers=headers, method="POST")
    audio = b""
    with urllib.request.urlopen(req, timeout=60) as r:
        for line in r:
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except Exception:
                continue
            if obj.get("data"):
                audio += base64.b64decode(obj["data"])
    assert audio, "TTS 无音频返回"
    with open(out_path, "wb") as f:
        f.write(audio)


def main():
    import sys
    topic = sys.argv[1] if len(sys.argv) > 1 else None
    os.makedirs(f"{HERE}/out", exist_ok=True)
    text = glm(topic)
    stamp = time.strftime("%Y%m%d_%H%M")
    out = f"{HERE}/out/电台_{stamp}.mp3"
    tts(text, out)
    with open(f"{HERE}/out/电台_{stamp}.txt", "w", encoding="utf-8") as f:
        f.write(text + "\n")
    print(f"稿子: {text}")
    print(f"音频: {out}")


if __name__ == "__main__":
    main()
