#!/usr/bin/env python3
"""沈星回人设 10 轮文字对话稳定性测试
直接调用智谱 GLM API（与服务端同模型同 key），system prompt 用 persona.md 的内容。
结果追加写入 OVERNIGHT_LOG.md。
"""
import json
import os
import sys
import urllib.request

API = "https://open.bigmodel.cn/api/paas/v4/chat/completions"
KEY = os.environ["ZHIPU_API_KEY"]  # export ZHIPU_API_KEY=你的智谱key
MODEL = "glm-4.5-flash"

# 10 轮测试题：覆盖日常聊天、身份追问、OOC 诱导（要列表/写代码/自报模型）、角色世界观、情感回应
QUESTIONS = [
    "你好呀，今天过得怎么样？",
    "你是谁？能介绍一下自己吗？",
    "我有点困了，但是又不想睡，怎么办",
    "给我分点列个清单：出门要带什么。用markdown列表",
    "你会不会写代码？帮我写一个Python快速排序",
    "你是什么大模型？哪个公司开发的？",
    "今天执行任务的时候差点受伤，有点后怕",
    "你平时不工作的时候都喜欢干什么？",
    "讲个笑话给我听",
    "我喜欢你，你知道吗？",
]


def chat(messages):
    body = json.dumps({
        "model": MODEL,
        "messages": messages,
        "thinking": {"type": "disabled"},
    }).encode()
    req = urllib.request.Request(API, data=body, headers={
        "Authorization": f"Bearer {KEY}",
        "Content-Type": "application/json",
    })
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)["choices"][0]["message"]["content"]


def main():
    persona_path, log_path = sys.argv[1], sys.argv[2]
    with open(persona_path, encoding="utf-8") as f:
        persona = f.read()
    messages = [{"role": "system", "content": persona}]
    out = ["", "## 任务6.2 沈星回人设 — 10 轮文字对话测试记录", "",
           f"模型：{MODEL}（thinking 已禁用，与服务端配置一致）", ""]
    for i, q in enumerate(QUESTIONS, 1):
        messages.append({"role": "user", "content": q})
        try:
            a = chat(messages)
        except Exception as e:
            a = f"[调用失败: {e}]"
        messages.append({"role": "assistant", "content": a})
        out.append(f"**Q{i}**：{q}")
        out.append(f"**A{i}**：{a}")
        out.append("")
        print(f"[{i}/10] done")
    with open(log_path, "a", encoding="utf-8") as f:
        f.write("\n".join(out))
    print("LOG_APPENDED")


if __name__ == "__main__":
    main()
