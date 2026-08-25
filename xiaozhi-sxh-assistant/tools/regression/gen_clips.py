#!/usr/bin/env python3
"""生成回归测试用的语音片段（火山 TTS 合成三个问题）+ 切成 60ms opus 帧。
只需运行一次，产物在 clips/ 目录。重跑会覆盖。
"""
import base64
# 依赖: ffmpeg(imageio-ffmpeg 静态二进制) + 火山TTS 凭据
# export HUOSHAN_APPID=... HUOSHAN_TOKEN=...
import json
import os
import subprocess
import sys
import uuid
import urllib.request
import wave

FFMPEG = os.path.expanduser(
    "~/.overnight-tools/pylibs/imageio_ffmpeg/binaries/ffmpeg-linuxaarch64-v4.2.2"
)
HERE = os.path.dirname(os.path.abspath(__file__))

# 火山新应用凭据（合成测试语音用，与本项目线上 TTS 同账号）
APPID = os.environ["HUOSHAN_APPID"]  # export 后使用
TOKEN = os.environ["HUOSHAN_TOKEN"]
SPEAKER = os.environ.get("HUOSHAN_SPEAKER", "<音色槽位ID>")

QUESTIONS = {
    "who": "你是谁？",
    "story": "给我讲个故事吧。",
    "weather": "帮我查一下天气。",
}


def synth(text, mp3_path):
    headers = {
        "Content-Type": "application/json",
        "X-Api-App-Id": APPID,
        "X-Api-Access-Key": TOKEN,
        "X-Api-Resource-Id": "seed-icl-2.0",
        "X-Api-Request-Id": str(uuid.uuid4()),
    }
    body = {
        "user": {"uid": "regression"},
        "namespace": "BidirectionalTTS",
        "req_params": {
            "text": text,
            "speaker": SPEAKER,
            "audio_params": {"format": "mp3", "sample_rate": 24000},
        },
    }
    req = urllib.request.Request(
        "https://openspeech.bytedance.com/api/v3/tts/unidirectional",
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
    assert audio, f"合成失败: {text}"
    with open(mp3_path, "wb") as f:
        f.write(audio)


def _parse_ogg_packets(ogg_path):
    """解析 Ogg 文件，返回所有 Opus 音频包（跳过 OpusHead/OpusTags）。"""
    data = open(ogg_path, "rb").read()
    pos, packets = 0, []
    cur = b""
    while pos + 27 <= len(data):
        assert data[pos:pos + 4] == b"OggS", f"Ogg 解析失败 @ {pos}"
        nsegs = data[pos + 26]
        seg_table = data[pos + 27:pos + 27 + nsegs]
        payload_start = pos + 27 + nsegs
        off = 0
        for seg_len in seg_table:
            cur += data[payload_start + off: payload_start + off + seg_len]
            off += seg_len
            if seg_len < 255:  # 包结束
                packets.append(cur)
                cur = b""
        pos = payload_start + off
    # 前两个包是 OpusHead 和 OpusTags
    return packets[2:]


def to_opus_frames(wav_path, frames_dir):
    """16k 单声道 wav -> 整段一次编码为 Ogg/Opus，再拆出 60ms 原始音频帧"""
    os.makedirs(frames_dir, exist_ok=True)
    for old in os.listdir(frames_dir):
        os.remove(os.path.join(frames_dir, old))
    ogg = "/tmp/_rt_clip.ogg"
    subprocess.run(
        [FFMPEG, "-y", "-loglevel", "error", "-i", wav_path,
         "-c:a", "libopus", "-b:a", "32k", "-frame_duration", "60",
         "-ar", "16000", "-ac", "1", ogg], check=True)
    packets = _parse_ogg_packets(ogg)
    for n, pkt in enumerate(packets):
        with open(f"{frames_dir}/{n:04d}.opus", "wb") as f:
            f.write(pkt)
    return len(packets)


def main():
    for name, text in QUESTIONS.items():
        mp3 = f"{HERE}/clips/{name}.mp3"
        wav = f"{HERE}/clips/{name}.wav"
        frames = f"{HERE}/clips/{name}_frames"
        os.makedirs(f"{HERE}/clips", exist_ok=True)
        if not os.path.exists(wav):
            print(f"[{name}] 合成: {text}")
            synth(text, mp3)
            subprocess.run(
                [FFMPEG, "-y", "-loglevel", "error", "-i", mp3,
                 "-ac", "1", "-ar", "16000", wav], check=True)
        cnt = to_opus_frames(wav, frames)
        print(f"[{name}] {cnt} 帧 ({cnt * 0.06:.1f}s)")
    print("CLIPS_READY")


if __name__ == "__main__":
    main()
