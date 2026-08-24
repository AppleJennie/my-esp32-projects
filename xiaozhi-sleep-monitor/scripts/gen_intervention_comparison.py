"""
智能枕头干预前后对比图 — 基于融合算法评分体系
生成自包含 HTML 报告，展示 30 晚数据：15 晚干预前基线 + 15 晚干预后
"""
import json
import math
import os
import random
from datetime import datetime, timedelta

random.seed(2026)

OUT_DIR = os.path.join(os.path.dirname(__file__), "out")
TOTAL_NIGHTS = 30
INTERVENTION_START = 15  # 0-based index: night 15 is first intervention night

# ── 融合算法阈值（与 sleep_fusion.c / snore_resp_score.cpp 对齐）──
THRESHOLDS = {
    "snore_prob_active": 0.55,       # sleep_fusion.c: snore_prob >= 0.55 → snore active
    "apnea_drop_ratio": 0.90,        # 幅度降至基线 10% 以下 → 疑似暂停
    "hypopnea_drop_ratio": 0.30,     # 幅度降至基线 30%~70% → 疑似低通气
    "body_motion_arousal": 30,       # sleep_fusion.c: body_motion > 30 → arousal
    "body_motion_baseline_max": 15,  # sleep_baseline.c: motion < 15 才能收集基线
    "spo2_hypoxia": 90.0,            # SpO2 < 90 → 低氧风险
    "rrei_mild": 5.0,                # rREI >= 5 → 病理性阈值
    "rrei_moderate": 15.0,
    "rrei_severe": 30.0,
    "score_perfect": 90,
    "score_mild": 75,
    "score_moderate": 50,
    "score_severe": 30,
}


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def sigmoid(x, k=0.5, x0=4.0):
    """平滑 S 曲线: 0..1"""
    return 1.0 / (1.0 + math.exp(-k * (x - x0)))


# ═══════════════════════════════════════════════════════════════
# 枕头状态机
# ═══════════════════════════════════════════════════════════════
class PillowState:
    def __init__(self):
        self.height_cm = 0.0        # 当前高度（cm）
        self.angle_deg = 0.0        # 当前角度（度）
        self.height_target = 0.0    # 目标高度
        self.angle_target = 0.0     # 目标角度
        self.max_height = 8.0       # 最大高度 cm
        self.max_angle = 15.0       # 最大角度 deg
        self.height_rate = 1.5      # 每晚上调最大速率 cm
        self.angle_rate = 3.0       # 每晚上调最大速率 deg
        self.relax_rate_h = 0.4     # 良好时回落速率 cm
        self.relax_rate_a = 0.8     # 良好时回落速率 deg
        self.last_trigger = "none"

    def adjust(self, night_data):
        """根据上一晚数据调整枕头目标"""
        triggers = []

        # 鼾声触发 → 加高度
        if night_data["snore_score"] > THRESHOLDS["snore_prob_active"]:
            self.height_target += 1.2
            triggers.append("snore")

        # 呼吸暂停触发 → 加角度
        if night_data["min_breath_amp_ratio"] < (1.0 - THRESHOLDS["apnea_drop_ratio"]):
            self.angle_target += 3.5
            triggers.append("apnea")
        elif night_data["min_breath_amp_ratio"] < (1.0 - THRESHOLDS["hypopnea_drop_ratio"]):
            self.angle_target += 1.8
            triggers.append("hypopnea")

        # SpO2 下降 → 优先加高度
        if night_data["spo2_min"] < THRESHOLDS["spo2_hypoxia"]:
            self.height_target += 1.8
            triggers.append("spo2_drop")

        # 体动过多 → 加高度（改善舒适度）
        if night_data["body_motion"] > THRESHOLDS["body_motion_arousal"]:
            self.height_target += 0.6
            triggers.append("motion_high")

        # 良好夜间 → 逐步回落
        if night_data["rrei"] < 3.0 and night_data["spo2_min"] > 93 and night_data["snore_score"] < 0.3:
            self.height_target -= self.relax_rate_h
            self.angle_target -= self.relax_rate_a
            if len(triggers) == 0:
                triggers.append("relax")

        # 钳位
        self.height_target = clamp(self.height_target, 0, self.max_height)
        self.angle_target = clamp(self.angle_target, 0, self.max_angle)

        # 向目标移动（受速率限制）
        self.height_cm = clamp(
            self.height_cm + clamp(self.height_target - self.height_cm, -self.height_rate, self.height_rate),
            0, self.max_height)
        self.angle_deg = clamp(
            self.angle_deg + clamp(self.angle_target - self.angle_deg, -self.angle_rate, self.angle_rate),
            0, self.max_angle)

        self.last_trigger = "+".join(triggers) if triggers else "none"
        return self.last_trigger

    def get_adjustment_factors(self):
        """返回枕头对睡眠指标的修正因子（0~1 之间，越大越改善）"""
        h = self.height_cm / self.max_height   # 0~1
        a = self.angle_deg / self.max_angle    # 0~1

        return {
            "snore_factor": clamp(1.0 - h * 0.65 - a * 0.2, 0.15, 1.0),
            "breath_amp_factor": clamp(1.0 + a * 0.7 + h * 0.45, 0.3, 1.6),
            "spo2_factor": clamp(1.0 + h * 0.12 + a * 0.06, 1.0, 1.15),
            "motion_factor": clamp(1.0 - h * 0.55 - a * 0.15, 0.4, 1.0),
            "rrei_factor": clamp(1.0 - h * 0.55 - a * 0.45, 0.08, 1.0),
        }


# ═══════════════════════════════════════════════════════════════
# 单晚数据生成
# ═══════════════════════════════════════════════════════════════
def generate_raw_metrics(phase):
    """生成无干预的原始睡眠指标"""
    if phase == "baseline":
        scenario = random.choices(
            ["mild", "moderate", "severe"],
            weights=[0.20, 0.45, 0.35]
        )[0]
    elif phase == "adjustment":
        scenario = random.choices(
            ["normal", "mild", "moderate", "severe"],
            weights=[0.20, 0.40, 0.28, 0.12]
        )[0]
    else:  # stable
        scenario = random.choices(
            ["normal", "mild", "moderate", "severe"],
            weights=[0.55, 0.30, 0.10, 0.05]
        )[0]

    # 基础指标（被枕头修正前）
    if scenario == "normal":
        rrei = round(random.uniform(0, 4.9), 1)
        apnea_like = random.randint(0, 3)
        hypopnea_like = random.randint(0, 8)
        spo2_min = random.randint(93, 98)
        spo2_avg = spo2_min + random.uniform(2.0, 5.0)
        breath_amp = round(random.uniform(0.85, 1.20), 2)
        min_breath_amp_ratio = round(random.uniform(0.55, 0.85), 2)
        body_motion = random.randint(5, 18)
        large_motion = int(body_motion * random.uniform(0.03, 0.10))
        snore_score = round(random.uniform(0.03, 0.30), 2)
        snore_count = random.randint(0, 10)
        hr_delta = round(random.uniform(1, 8), 0)
        no_hr_response = False
    elif scenario == "mild":
        rrei = round(random.uniform(5.0, 14.9), 1)
        apnea_like = random.randint(3, 10)
        hypopnea_like = random.randint(8, 25)
        spo2_min = random.randint(89, 94)
        spo2_avg = spo2_min + random.uniform(2.0, 5.0)
        breath_amp = round(random.uniform(0.55, 0.90), 2)
        min_breath_amp_ratio = round(random.uniform(0.30, 0.65), 2)
        body_motion = random.randint(12, 30)
        large_motion = int(body_motion * random.uniform(0.05, 0.18))
        snore_score = round(random.uniform(0.25, 0.60), 2)
        snore_count = random.randint(8, 25)
        hr_delta = round(random.uniform(5, 15), 0)
        no_hr_response = random.random() < 0.10
    elif scenario == "moderate":
        rrei = round(random.uniform(15.0, 29.9), 1)
        apnea_like = random.randint(10, 28)
        hypopnea_like = random.randint(25, 55)
        spo2_min = random.randint(85, 90)
        spo2_avg = spo2_min + random.uniform(2.0, 5.0)
        breath_amp = round(random.uniform(0.25, 0.60), 2)
        min_breath_amp_ratio = round(random.uniform(0.10, 0.35), 2)
        body_motion = random.randint(20, 45)
        large_motion = int(body_motion * random.uniform(0.08, 0.22))
        snore_score = round(random.uniform(0.50, 0.85), 2)
        snore_count = random.randint(20, 50)
        hr_delta = round(random.uniform(15, 32), 0)
        no_hr_response = random.random() < 0.12
    else:  # severe
        rrei = round(random.uniform(30.0, 45.0), 1)
        apnea_like = random.randint(25, 52)
        hypopnea_like = random.randint(50, 82)
        spo2_min = random.randint(78, 86)
        spo2_avg = spo2_min + random.uniform(1.0, 4.0)
        breath_amp = round(random.uniform(0.10, 0.35), 2)
        min_breath_amp_ratio = round(random.uniform(0.03, 0.18), 2)
        body_motion = random.randint(30, 60)
        large_motion = int(body_motion * random.uniform(0.12, 0.30))
        snore_score = round(random.uniform(0.65, 0.96), 2)
        snore_count = random.randint(35, 65)
        hr_delta = round(random.uniform(18, 38), 0)
        no_hr_response = random.random() < 0.15

    sleep_hours = round(random.uniform(5.5, 8.5), 1)
    turn_over = random.randint(2, 35)

    return {
        "scenario": scenario,
        "sleep_hours": sleep_hours,
        "rrei": rrei,
        "apnea_like": apnea_like,
        "hypopnea_like": hypopnea_like,
        "total_events": apnea_like + hypopnea_like,
        "spo2_min": spo2_min,
        "spo2_avg": round(clamp(spo2_avg, spo2_min, 99), 1),
        "breath_amp": breath_amp,
        "min_breath_amp_ratio": min_breath_amp_ratio,
        "body_motion": body_motion,
        "large_motion": large_motion,
        "snore_score": snore_score,
        "snore_count": snore_count,
        "hr_delta": hr_delta,
        "no_hr_response": no_hr_response,
        "turn_over": turn_over,
    }


def compute_fusion_score(raw, factors=None):
    """基于融合算法 3 维度评分（与 snore_resp_score.cpp 对齐）

    A. 呼吸事件负荷 0~50
    B. 低氧负荷 0~35
    C. 自主神经代偿 0~15
    总分 = A + B + C (0~100)
    """
    if factors is None:
        factors = {}

    rrei_adj = raw["rrei"] * factors.get("rrei_factor", 1.0)
    spo2_min_adj = raw["spo2_min"] * factors.get("spo2_factor", 1.0)
    spo2_min_adj = min(spo2_min_adj, 99)

    # A. 事件负荷 (0~50)
    r = rrei_adj
    if r <= 0:
        event_load = 50
    elif r <= 5:
        event_load = 45
    elif r <= 15:
        event_load = clamp(45 - int((r - 5.0) / 10.0 * 15.0), 30, 45)
    elif r <= 30:
        event_load = clamp(30 - int((r - 15.0) / 15.0 * 20.0), 10, 30)
    else:
        event_load = clamp(10 - int((r - 30.0) * 0.33), 0, 10)

    # B. 低氧负荷 (0~35)
    if spo2_min_adj >= 95:
        hypoxia = 35
    elif spo2_min_adj >= 90:
        hypoxia = 28
    elif spo2_min_adj >= 88:
        hypoxia = 18
    elif spo2_min_adj >= 85:
        hypoxia = 10
    else:
        hypoxia = clamp(6 - int((85.0 - spo2_min_adj) * 1.2), 0, 6)

    # C. 自主神经代偿 (0~15)
    if raw["no_hr_response"]:
        autonomic = 0
    elif raw["hr_delta"] <= 10:
        autonomic = 15
    elif raw["hr_delta"] <= 20:
        autonomic = 10
    else:
        autonomic = 5

    total = clamp(event_load + hypoxia + autonomic, 0, 100)
    return total, event_load, hypoxia, autonomic


def risk_level_str(score):
    if score >= 90:
        return "完美"
    elif score >= 75:
        return "观察级"
    elif score >= 50:
        return "建议关注"
    elif score >= 30:
        return "建议就医"
    else:
        return "建议尽快评估"


def generate_all_nights():
    """生成 30 晚完整数据"""
    pillow = PillowState()
    nights = []
    pillow_log = []

    start_date = datetime(2026, 6, 7)

    # 预生成所有无干预原始数据
    raw_data = []
    for i in range(TOTAL_NIGHTS):
        if i < INTERVENTION_START:
            phase = "baseline"
        elif i < INTERVENTION_START + 10:
            phase = "adjustment"
        else:
            phase = "stable"
        raw_data.append(generate_raw_metrics(phase))

    # 逐晚处理：枕头基于上一晚数据调整，然后修正当晚报指标
    for i in range(TOTAL_NIGHTS):
        raw = raw_data[i]
        date = start_date + timedelta(days=i)
        date_str = date.strftime("%m/%d")

        # 获取枕头修正因子（基于上一晚调整后的枕头状态）
        factors = pillow.get_adjustment_factors() if i >= INTERVENTION_START else {}

        # 应用修正因子
        if i >= INTERVENTION_START:
            snore_adj = clamp(raw["snore_score"] * factors["snore_factor"], 0.01, 0.98)
            breath_amp_adj = clamp(raw["breath_amp"] * factors["breath_amp_factor"], 0.05, 1.50)
            spo2_min_adj = clamp(raw["spo2_min"] * factors["spo2_factor"], 80, 99)
            spo2_avg_adj = clamp(raw["spo2_avg"] * factors["spo2_factor"], 85, 99)
            motion_adj = clamp(raw["body_motion"] * factors["motion_factor"], 3, 55)
            large_motion_adj = clamp(raw["large_motion"] * factors["motion_factor"], 0, 20)
            rrei_adj = clamp(raw["rrei"] * factors["rrei_factor"], 0.1, 40)
            apnea_adj = max(0, int(raw["apnea_like"] * factors["rrei_factor"]))
            hypopnea_adj = max(0, int(raw["hypopnea_like"] * factors["rrei_factor"]))
            snore_count_adj = max(0, int(raw["snore_count"] * factors["snore_factor"]))
            turn_over_adj = clamp(raw["turn_over"] * factors["motion_factor"], 1, 30)
        else:
            snore_adj = raw["snore_score"]
            breath_amp_adj = raw["breath_amp"]
            spo2_min_adj = raw["spo2_min"]
            spo2_avg_adj = raw["spo2_avg"]
            motion_adj = raw["body_motion"]
            large_motion_adj = raw["large_motion"]
            rrei_adj = raw["rrei"]
            apnea_adj = raw["apnea_like"]
            hypopnea_adj = raw["hypopnea_like"]
            snore_count_adj = raw["snore_count"]
            turn_over_adj = raw["turn_over"]

        # 计算融合评分
        score, event_load, hypoxia, autonomic = compute_fusion_score(raw, factors if i >= INTERVENTION_START else None)
        risk = risk_level_str(score)

        night = {
            "night": i,
            "date": date_str,
            "phase": "pre" if i < INTERVENTION_START else "post",
            "snore_score": round(snore_adj, 2),
            "breath_amp": round(breath_amp_adj, 2),
            "min_breath_amp_ratio": round(raw["min_breath_amp_ratio"], 2),
            "spo2_min": round(spo2_min_adj, 1),
            "spo2_avg": round(spo2_avg_adj, 1),
            "body_motion": int(motion_adj),
            "large_motion": int(large_motion_adj),
            "rrei": round(rrei_adj, 1),
            "apnea_like": apnea_adj,
            "hypopnea_like": hypopnea_adj,
            "snore_count": snore_count_adj,
            "turn_over": int(turn_over_adj),
            "hr_delta": raw["hr_delta"],
            "no_hr_response": raw["no_hr_response"],
            "score": score,
            "event_load": event_load,
            "hypoxia": hypoxia,
            "autonomic": autonomic,
            "risk_level": risk,
            "sleep_hours": raw["sleep_hours"],
            "scenario": raw["scenario"],
        }
        nights.append(night)

        # 枕头调节（基于当前修正后的数据，为下一晚做准备）
        trigger = pillow.adjust(night) if i >= INTERVENTION_START else "none"
        pillow_log.append({
            "night": i,
            "date": date_str,
            "height_cm": round(pillow.height_cm, 1),
            "angle_deg": round(pillow.angle_deg, 1),
            "height_target": round(pillow.height_target, 1),
            "angle_target": round(pillow.angle_target, 1),
            "trigger": trigger,
        })

    return nights, pillow_log


# ═══════════════════════════════════════════════════════════════
# HTML 生成
# ═══════════════════════════════════════════════════════════════
def build_html(nights, pillow_log):
    # 计算统计
    pre = nights[:INTERVENTION_START]
    post = nights[INTERVENTION_START:]
    pre_score_avg = sum(n["score"] for n in pre) / len(pre)
    post_score_avg = sum(n["score"] for n in post) / len(post)
    pre_rrei_avg = sum(n["rrei"] for n in pre) / len(pre)
    post_rrei_avg = sum(n["rrei"] for n in post) / len(post)
    pre_spo2_min = min(n["spo2_min"] for n in pre)
    post_spo2_min = min(n["spo2_min"] for n in post)
    pre_motion_avg = sum(n["body_motion"] for n in pre) / len(pre)
    post_motion_avg = sum(n["body_motion"] for n in post) / len(post)
    final_height = pillow_log[-1]["height_cm"]
    final_angle = pillow_log[-1]["angle_deg"]
    adj_count = sum(1 for p in pillow_log if p["trigger"] != "none" and p["trigger"] != "relax")

    nights_json = json.dumps(nights, ensure_ascii=False)
    pillow_json = json.dumps(pillow_log, ensure_ascii=False)
    intervention_night = INTERVENTION_START

    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>智能枕头干预前后对比 — 融合算法评分体系</title>
<style>
:root {{
  --surface: #fcfcfb; --surface2: #f5f4f1; --text-primary: #0b0b0b;
  --text-secondary: #52514e; --text-muted: #8b8a85;
  --grid: #e5e4e1; --axis: #c3c2b7;
  --blue: #2a78d6; --blue-fill: rgba(42,120,214,0.07);
  --red: #dc2626; --red-fill: rgba(220,38,38,0.07);
  --green: #10b981; --green-fill: rgba(16,185,129,0.07);
  --purple: #8b5cf6; --purple-fill: rgba(139,92,246,0.07);
  --orange: #f59e0b; --orange-fill: rgba(245,158,11,0.07);
  --teal: #06b6d4; --teal-fill: rgba(6,182,212,0.07);
  --pink: #ec4899; --pink-fill: rgba(236,72,153,0.07);
  --pre-bg: rgba(239,68,68,0.04); --post-bg: rgba(16,185,129,0.04);
  --intervention-line: #ef4444;
  --card-good: #10b981; --card-bad: #ef4444; --card-neutral: #6b7280;
  --shadow: 0 1px 3px rgba(0,0,0,0.08);
  --shadow-lg: 0 4px 16px rgba(0,0,0,0.10);
}}
@media (prefers-color-scheme: dark) {{
:root {{
  --surface: #1a1a19; --surface2: #222220; --text-primary: #f0f0ee;
  --text-secondary: #c3c2b7; --text-muted: #6b6a65;
  --grid: #2d2d2b; --axis: #52514e;
  --blue: #3987e5; --blue-fill: rgba(57,135,229,0.10);
  --red: #ef4444; --red-fill: rgba(239,68,68,0.10);
  --green: #34d399; --green-fill: rgba(52,211,153,0.10);
  --purple: #a78bfa; --purple-fill: rgba(167,139,250,0.10);
  --orange: #fbbf24; --orange-fill: rgba(251,191,36,0.10);
  --teal: #22d3ee; --teal-fill: rgba(34,211,238,0.10);
  --pink: #f472b6; --pink-fill: rgba(244,114,182,0.10);
  --pre-bg: rgba(239,68,68,0.07); --post-bg: rgba(52,211,153,0.07);
  --intervention-line: #f87171;
  --shadow: 0 1px 3px rgba(0,0,0,0.4);
  --shadow-lg: 0 4px 16px rgba(0,0,0,0.5);
}}}}
* {{ box-sizing: border-box; margin: 0; padding: 0; }}
body {{
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto,
    "Noto Sans SC", "PingFang SC", "Microsoft YaHei", sans-serif;
  background: var(--surface); color: var(--text-primary);
  display: flex; justify-content: center; padding: 24px;
}}
.container {{ max-width: 1200px; width: 100%; }}
h1 {{ font-size: 22px; font-weight: 700; }}
.subtitle {{ color: var(--text-secondary); font-size: 13px; margin: 4px 0 20px; }}
.stats-row {{ display: grid; grid-template-columns: repeat(5,1fr); gap: 12px; margin-bottom: 24px; }}
.stat-card {{
  background: var(--surface); border: 1px solid var(--grid);
  border-radius: 10px; padding: 16px; box-shadow: var(--shadow);
  text-align: center;
}}
.stat-label {{ font-size: 11px; color: var(--text-secondary); margin-bottom: 4px; }}
.stat-value {{ font-size: 26px; font-weight: 700; }}
.stat-delta {{ font-size: 12px; margin-top: 2px; }}
.stat-delta.up {{ color: var(--card-good); }}
.stat-delta.down {{ color: var(--card-bad); }}
.chart-panel {{
  background: var(--surface); border: 1px solid var(--grid);
  border-radius: 10px; padding: 20px 24px 12px; box-shadow: var(--shadow);
  margin-bottom: 16px; position: relative;
}}
.chart-title {{ font-size: 15px; font-weight: 600; margin-bottom: 2px; }}
.chart-subtitle {{ font-size: 11px; color: var(--text-secondary); margin-bottom: 8px; }}
.chart-row {{ display: grid; grid-template-columns: 1fr 1fr; gap: 16px; }}
.chart-container {{ position: relative; width: 100%; }}
svg {{ display: block; width: 100%; }}
.tooltip {{
  position: absolute; pointer-events: none; opacity: 0;
  background: var(--text-primary); color: var(--surface);
  font-size: 12px; padding: 8px 12px; border-radius: 8px;
  white-space: nowrap; z-index: 10; line-height: 1.5;
  font-family: "SF Mono","Consolas","Courier New",monospace;
  box-shadow: var(--shadow-lg);
}}
.legend {{ display: flex; gap: 16px; margin-top: 8px; flex-wrap: wrap; font-size: 11px; color: var(--text-secondary); }}
.legend-item {{ display: flex; align-items: center; gap: 5px; }}
.legend-swatch {{ width: 10px; height: 10px; border-radius: 2px; flex-shrink: 0; }}
.phase-bar {{
  display: flex; height: 4px; border-radius: 2px; overflow: hidden;
  margin-bottom: 16px;
}}
.phase-pre {{ background: var(--intervention-line); flex: 1; }}
.phase-post {{ background: var(--green); flex: 1; }}
.footer-note {{ text-align: center; color: var(--text-muted); font-size: 11px; margin-top: 20px; padding-bottom: 16px; }}
</style>
</head>
<body>
<div class="container">
  <h1>智能枕头干预前后对比 — 基于融合算法评分体系</h1>
  <p class="subtitle">30 天睡眠监测数据 · 第 16 天（{nights[INTERVENTION_START]["date"]}）开始智能枕头干预 ·
    R60 毫米波雷达 + SpO₂ + 鼾声 AI 多证据融合</p>

  <div class="phase-bar">
    <div class="phase-pre" title="干预前基线期 15 天"></div>
    <div class="phase-post" title="干预后改善期 15 天"></div>
  </div>

  <!-- 统计卡片 -->
  <div class="stats-row">
    <div class="stat-card">
      <div class="stat-label">干预前 平均融合评分</div>
      <div class="stat-value" style="color:var(--card-bad)">{pre_score_avg:.0f}</div>
      <div class="stat-delta down">{pre[0]["risk_level"]} ~ {pre[-1]["risk_level"]}</div>
    </div>
    <div class="stat-card">
      <div class="stat-label">干预后 平均融合评分</div>
      <div class="stat-value" style="color:var(--card-good)">{post_score_avg:.0f}</div>
      <div class="stat-delta up">↑ +{post_score_avg - pre_score_avg:.0f} 分 ({(post_score_avg/pre_score_avg - 1)*100:.0f}%)</div>
    </div>
    <div class="stat-card">
      <div class="stat-label">rREI 改善</div>
      <div class="stat-value" style="color:var(--card-good)">{pre_rrei_avg:.1f}→{post_rrei_avg:.1f}</div>
      <div class="stat-delta down">↓ {(1 - post_rrei_avg/max(pre_rrei_avg,0.1))*100:.0f}%</div>
    </div>
    <div class="stat-card">
      <div class="stat-label">最低 SpO₂ 提升</div>
      <div class="stat-value" style="color:var(--card-good)">{pre_spo2_min:.0f}→{post_spo2_min:.0f}%</div>
      <div class="stat-delta up">↑ +{post_spo2_min - pre_spo2_min:.0f}%</div>
    </div>
    <div class="stat-card">
      <div class="stat-label">枕头调节摘要</div>
      <div class="stat-value" style="color:var(--teal)">{adj_count}次</div>
      <div class="stat-delta">高度{final_height:.1f}cm · 角度{final_angle:.0f}°</div>
    </div>
  </div>

  <!-- Panel A + B: 2 columns -->
  <div class="chart-row">
    <div class="chart-panel">
      <div class="chart-title">A. 融合评分 + rREI 双轴变化</div>
      <div class="chart-subtitle">融合评分 = 事件负荷(0~50) + 低氧(0~35) + 自主神经(0~15) · rREI 为雷达呼吸事件指数</div>
      <div class="chart-container" id="panelA"><div class="tooltip" id="ttA"></div></div>
      <div class="legend">
        <div class="legend-item"><span class="legend-swatch" style="background:var(--blue)"></span>融合评分 (0~100)</div>
        <div class="legend-item"><span class="legend-swatch" style="background:var(--red)"></span>rREI (次/小时)</div>
        <div class="legend-item"><span class="legend-swatch" style="background:var(--intervention-line);width:20px;height:1px;border-top:2px dashed var(--intervention-line)"></span>干预开始</div>
      </div>
    </div>
    <div class="chart-panel">
      <div class="chart-title">B. 呼吸幅度 + SpO₂ 双轴变化</div>
      <div class="chart-subtitle">呼吸幅度为归一化值（基线=1.0）· SpO₂ 为夜间最低值</div>
      <div class="chart-container" id="panelB"><div class="tooltip" id="ttB"></div></div>
      <div class="legend">
        <div class="legend-item"><span class="legend-swatch" style="background:var(--green)"></span>呼吸幅度 (norm)</div>
        <div class="legend-item"><span class="legend-swatch" style="background:var(--purple)"></span>最低 SpO₂ (%)</div>
      </div>
    </div>
  </div>

  <!-- Panel C + D: 2 columns -->
  <div class="chart-row">
    <div class="chart-panel">
      <div class="chart-title">C. 体动 + 大幅体动</div>
      <div class="chart-subtitle">体动次数来自雷达检测 · 虚线为融合引擎体动觉醒阈值 (30)</div>
      <div class="chart-container" id="panelC"><div class="tooltip" id="ttC"></div></div>
      <div class="legend">
        <div class="legend-item"><span class="legend-swatch" style="background:var(--orange)"></span>体动次数</div>
        <div class="legend-item"><span class="legend-swatch" style="background:var(--red)"></span>大幅体动</div>
      </div>
    </div>
    <div class="chart-panel">
      <div class="chart-title">D. 呼吸事件统计 (apnea + hypopnea)</div>
      <div class="chart-subtitle">雷达呼吸波形事件检测 · apnea=幅度降≥90% · hypopnea=幅度降30%~90%</div>
      <div class="chart-container" id="panelD"><div class="tooltip" id="ttD"></div></div>
      <div class="legend">
        <div class="legend-item"><span class="legend-swatch" style="background:var(--red)"></span>疑似呼吸暂停</div>
        <div class="legend-item"><span class="legend-swatch" style="background:var(--orange)"></span>疑似低通气</div>
      </div>
    </div>
  </div>

  <!-- Panel E: 枕头调节日志 (full width) -->
  <div class="chart-panel">
    <div class="chart-title">E. 枕头调节幅度日志</div>
    <div class="chart-subtitle">基于融合引擎输出自动调节 · 触发事件: 鼾声→加高度 / 呼吸暂停→加角度 / SpO₂下降→加高度</div>
    <div class="chart-container" id="panelE"><div class="tooltip" id="ttE"></div></div>
    <div class="legend">
      <div class="legend-item"><span class="legend-swatch" style="background:var(--teal)"></span>枕头高度 (cm)</div>
      <div class="legend-item"><span class="legend-swatch" style="background:var(--pink)"></span>枕头角度 (°)</div>
      <div class="legend-item"><span class="legend-swatch" style="background:var(--text-muted)" id="ttMarkerSwatch"></span>触发事件标注</div>
    </div>
  </div>

  <p class="footer-note">
    融合算法: A.呼吸事件负荷(0~50) + B.低氧负荷(0~35) + C.自主神经代偿(0~15) = 综合评分(0~100)
    · 风险等级: ≥90 完美 / ≥75 观察级 / ≥50 建议关注 / ≥30 建议就医 / &lt;30 建议尽快评估
    · ⚠️ 本演示数据为模拟生成，仅展示算法原理，不构成医学诊断
  </p>
</div>

<script>
// ── 数据 ──
const NIGHTS = {nights_json};
const PILLOW = {pillow_json};
const PRE_N = {intervention_night};
const TOTAL_N = {TOTAL_NIGHTS};

// ── SVG 工具函数 ──
const NS = "http://www.w3.org/2000/svg";
function el(tag, attrs = {{}}) {{
  const e = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  return e;
}}

// ── 通用 Chart 类 ──
class Chart {{
  constructor(containerId, W, H, M) {{
    this.W = W; this.H = H; this.M = M;
    this.PW = W - M.left - M.right;
    this.PH = H - M.top - M.bottom;
    this.series = [];
    this.hlines = [];
    this.vlines = [];
    this.div = document.getElementById(containerId);
    this.svg = el("svg", {{ viewBox: `0 0 ${{W}} ${{H}}`, xmlns: NS, overflow: "visible" }});
    this.div.appendChild(this.svg);
    this.xMin = 0; this.xMax = TOTAL_N - 1;
  }}

  tx(i) {{ return this.M.left + (i - this.xMin) / (this.xMax - this.xMin) * this.PW; }}
  ty(v, yMin, yMax) {{ return this.M.top + this.PH - (v - yMin) / (yMax - yMin) * this.PH; }}

  addBackgroundZones() {{
    // 干预前红色背景
    const x1 = this.tx(0), x2 = this.tx(PRE_N - 0.5);
    this.svg.appendChild(el("rect", {{ x: x1, y: this.M.top, width: x2 - x1, height: this.PH,
      fill: "var(--pre-bg)" }}));
    // 干预后绿色背景
    const x3 = this.tx(PRE_N - 0.5), x4 = this.tx(TOTAL_N - 1);
    this.svg.appendChild(el("rect", {{ x: x3, y: this.M.top, width: x4 - x3, height: this.PH,
      fill: "var(--post-bg)" }}));
    // 稳定期标记
    const xStable = this.tx(25);
    this.svg.appendChild(el("line", {{ x1: xStable, y1: this.M.top, x2: xStable, y2: this.M.top + this.PH,
      stroke: "var(--text-muted)", "stroke-width": "1", "stroke-dasharray": "2,4", opacity: "0.5" }}));
  }}

  drawGrid(yMin, yMax, yStep, leftLabel) {{
    for (let v = Math.ceil(yMin / yStep) * yStep; v <= yMax; v += yStep) {{
      const y = this.ty(v, yMin, yMax);
      this.svg.appendChild(el("line", {{ x1: this.M.left, y1: y, x2: this.M.left + this.PW, y2: y,
        stroke: "var(--grid)", "stroke-width": "1" }}));
      if (leftLabel) {{
        const lbl = el("text", {{ x: this.M.left - 8, y: y + 4, "text-anchor": "end",
          fill: "var(--text-secondary)", "font-size": "10" }});
        lbl.textContent = v.toFixed(v % 1 === 0 ? 0 : 1);
        this.svg.appendChild(lbl);
      }}
    }}
    // X axis labels every 5 nights
    for (let i = 0; i < TOTAL_N; i += 5) {{
      const x = this.tx(i);
      const lbl = el("text", {{ x, y: this.M.top + this.PH + 16, "text-anchor": "middle",
        fill: "var(--text-secondary)", "font-size": "10" }});
      lbl.textContent = NIGHTS[i].date;
      this.svg.appendChild(lbl);
    }}
  }}

  addHLine(yVal, yMin, yMax, label, color, dash) {{
    const y = this.ty(yVal, yMin, yMax);
    this.svg.appendChild(el("line", {{ x1: this.M.left, y1: y, x2: this.M.left + this.PW, y2: y,
      stroke: color, "stroke-width": "1.5", "stroke-dasharray": dash || "3,4", opacity: "0.6" }}));
    const lbl = el("text", {{ x: this.M.left + this.PW - 4, y: y - 5, "text-anchor": "end",
      fill: color, "font-size": "9", opacity: "0.75" }});
    lbl.textContent = label;
    this.svg.appendChild(lbl);
  }}

  addVLine(nightIdx, label, color, dash) {{
    const x = this.tx(nightIdx - 0.5);
    this.svg.appendChild(el("line", {{ x1: x, y1: this.M.top, x2: x, y2: this.M.top + this.PH,
      stroke: color, "stroke-width": "2", "stroke-dasharray": dash || "6,3", opacity: "0.8" }}));
    const lbl = el("text", {{ x: x + 6, y: this.M.top + 16, fill: color, "font-size": "11", "font-weight": "600" }});
    lbl.textContent = label;
    this.svg.appendChild(lbl);
  }}

  addLine(dataKey, yMin, yMax, color, rightAxis, label) {{
    let d = "";
    for (let i = 0; i < TOTAL_N; i++) {{
      const v = NIGHTS[i][dataKey];
      const x = this.tx(i), y = this.ty(v, yMin, yMax);
      d += `${{i === 0 ? "M" : "L"}}${{x.toFixed(1)}},${{y.toFixed(1)}} `;
    }}
    const path = el("path", {{ d, fill: "none", stroke: color, "stroke-width": "2.5",
      "stroke-linejoin": "round", "stroke-linecap": "round" }});
    path.dataset.series = dataKey;
    this.svg.appendChild(path);
    this.series.push({{ key: dataKey, yMin, yMax, color, label }});
  }}

  addArea(dataKey, yMin, yMax, color) {{
    let d = "";
    for (let i = 0; i < TOTAL_N; i++) {{
      const v = NIGHTS[i][dataKey];
      const x = this.tx(i), y = this.ty(v, yMin, yMax);
      d += `${{i === 0 ? "M" : "L"}}${{x.toFixed(1)}},${{y.toFixed(1)}} `;
    }}
    d += `L${{this.tx(TOTAL_N-1).toFixed(1)}},${{this.ty(yMin, yMin, yMax)}} L${{this.tx(0).toFixed(1)}},${{this.ty(yMin, yMin, yMax)}} Z`;
    this.svg.appendChild(el("path", {{ d, fill: color, stroke: "none" }}));
  }}

  addBars(dataKey, yMin, yMax, color, barWidth) {{
    const bw = barWidth || (this.PW / TOTAL_N * 0.6);
    for (let i = 0; i < TOTAL_N; i++) {{
      const v = NIGHTS[i][dataKey];
      const x = this.tx(i) - bw / 2;
      const y = this.ty(v, yMin, yMax);
      const h = this.ty(yMin, yMin, yMax) - y;
      const bar = el("rect", {{ x, y, width: bw, height: Math.max(0, h),
        fill: color, rx: "1.5", opacity: "0.75" }});
      bar.dataset.series = dataKey;
      bar.dataset.night = i;
      this.svg.appendChild(bar);
    }}
    this.series.push({{ key: dataKey, yMin, yMax, color, label: dataKey }});
  }}

  addPillowBars(yMin, yMax, color) {{
    const bw = this.PW / TOTAL_N * 0.6;
    for (let i = 0; i < TOTAL_N; i++) {{
      const v = PILLOW[i].height_cm;
      if (v <= 0) continue;
      const x = this.tx(i) - bw / 2;
      const y = this.ty(v, yMin, yMax);
      const h = this.ty(0, yMin, yMax) - y;
      this.svg.appendChild(el("rect", {{ x, y, width: bw, height: Math.max(0, h),
        fill: color, rx: "1.5", opacity: "0.7" }}));
    }}
  }}

  addPillowAngleLine(yMin, yMax, color) {{
    let d = "";
    for (let i = 0; i < TOTAL_N; i++) {{
      const v = PILLOW[i].angle_deg;
      const x = this.tx(i), y = this.ty(v, yMin, yMax);
      d += `${{i === 0 ? "M" : "L"}}${{x.toFixed(1)}},${{y.toFixed(1)}} `;
    }}
    this.svg.appendChild(el("path", {{ d, fill: "none", stroke: color, "stroke-width": "2.5",
      "stroke-linejoin": "round", "stroke-linecap": "round" }}));
    // dots at adjustment points
    for (let i = 0; i < TOTAL_N; i++) {{
      if (PILLOW[i].trigger !== "none" && PILLOW[i].trigger !== "relax") {{
        const v = PILLOW[i].angle_deg;
        const x = this.tx(i), y = this.ty(v, yMin, yMax);
        this.svg.appendChild(el("circle", {{ cx: x, cy: y, r: "4",
          fill: color, stroke: "var(--surface)", "stroke-width": "1.5" }}));
      }}
    }}
  }}

  addPillowTriggerLabels(yMin, yMax) {{
    const labels = {{ "snore": "鼾", "apnea": "停", "hypopnea": "低", "spo2_drop": "氧", "motion_high": "动" }};
    for (let i = 0; i < TOTAL_N; i++) {{
      const t = PILLOW[i].trigger;
      if (t === "none" || t === "relax") continue;
      const triggers = t.split("+");
      const x = this.tx(i);
      const h = PILLOW[i].height_cm;
      const baseY = this.ty(h > 0.3 ? h : 0.5, yMin, yMax) - 10;
      triggers.forEach((tr, idx) => {{
        const short = labels[tr] || tr[0];
        const lbl = el("text", {{ x, y: baseY - idx * 13, "text-anchor": "middle",
          fill: "var(--text-secondary)", "font-size": "9", "font-weight": "600" }});
        lbl.textContent = short;
        this.svg.appendChild(lbl);
      }});
    }}
  }}

  drawAxis(yMin, yMax) {{
    this.svg.appendChild(el("rect", {{ x: this.M.left, y: this.M.top,
      width: this.PW, height: this.PH, fill: "none", stroke: "var(--axis)", "stroke-width": "1" }}));
  }}

  createRightAxis(yMin, yMax, label, color) {{
    // draw right-side values
    const step = (yMax - yMin) / 5;
    for (let v = yMin; v <= yMax + step * 0.5; v += step) {{
      const y = this.ty(v, yMin, yMax);
      const lbl = el("text", {{ x: this.M.left + this.PW + 8, y: y + 4, "text-anchor": "start",
        fill: color, "font-size": "10", "font-weight": "500" }});
      lbl.textContent = v.toFixed(v % 1 === 0 ? 0 : 1);
      this.svg.appendChild(lbl);
    }}
    // Label
    const lblEl = el("text", {{ x: this.M.left + this.PW + 32, y: this.M.top + this.PH / 2,
      "text-anchor": "middle", fill: color, "font-size": "11", "font-weight": "600",
      transform: `rotate(90,${{this.M.left + this.PW + 32}},${{this.M.top + this.PH / 2}})` }});
    lblEl.textContent = label;
    this.svg.appendChild(lblEl);
  }}

  createLeftAxis(yMin, yMax, label, color) {{
    const lblEl = el("text", {{ x: 12, y: this.M.top + this.PH / 2,
      "text-anchor": "middle", fill: color, "font-size": "11", "font-weight": "600",
      transform: `rotate(-90,12,${{this.M.top + this.PH / 2}})` }});
    lblEl.textContent = label;
    this.svg.appendChild(lblEl);
  }}

  createHoverTooltip(tooltipId, getHtml) {{
    const tooltip = document.getElementById(tooltipId);
    const svgEl = this.svg;
    const chartDiv = this.div;
    const self = this;

    svgEl.addEventListener("mousemove", (e) => {{
      const rect = svgEl.getBoundingClientRect();
      const scaleX = self.W / rect.width;
      const svgX = (e.clientX - rect.left) * scaleX;
      let best = -1, bestDist = Infinity;
      for (let i = 0; i < TOTAL_N; i++) {{
        const dist = Math.abs(self.tx(i) - svgX);
        if (dist < bestDist) {{ bestDist = dist; best = i; }}
      }}
      if (bestDist < 30 * scaleX && best >= 0) {{
        tooltip.style.opacity = "1";
        tooltip.style.left = (e.clientX - chartDiv.getBoundingClientRect().left + 14) + "px";
        tooltip.style.top = (e.clientY - chartDiv.getBoundingClientRect().top - 50) + "px";
        tooltip.innerHTML = getHtml(best);
      }}
    }});
    svgEl.addEventListener("mouseleave", () => {{ tooltip.style.opacity = "0"; }});
  }}
}}

// ═══════════════════════════════════════════════════════════════
// Panel A: 融合评分 + rREI 双轴
// ═══════════════════════════════════════════════════════════════
(() => {{
  const M = {{ top: 48, right: 60, bottom: 38, left: 54 }};
  const chart = new Chart("panelA", 600, 320, M);
  const scoreMin = 25, scoreMax = 100;
  const rreiMin = 0, rreiMax = 42;

  chart.addBackgroundZones();
  chart.drawGrid(scoreMin, scoreMax, 10, true);
  chart.addArea("score", scoreMin, scoreMax, "var(--blue-fill)");
  chart.addLine("score", scoreMin, scoreMax, "var(--blue)", false, "融合评分");
  chart.createRightAxis(rreiMin, rreiMax, "rREI (次/小时)", "var(--red)");
  chart.addLine("rrei", rreiMin, rreiMax, "var(--red)", true, "rREI");
  chart.addVLine(PRE_N, "干预开始", "var(--intervention-line)", "6,3");
  chart.addHLine(THRESH_SCORE_MODERATE, scoreMin, scoreMax, "建议关注(50)", "var(--orange)", "3,4");
  chart.addHLine(THRESH_SCORE_MILD, scoreMin, scoreMax, "观察级(75)", "var(--teal)", "3,4");
  chart.createLeftAxis(scoreMin, scoreMax, "融合评分 (0~100)", "var(--blue)");
  chart.drawAxis(scoreMin, scoreMax);
  chart.createHoverTooltip("ttA", (i) => {{
    const n = NIGHTS[i];
    return `第${{i+1}}晚 ${{n.date}}<br/>`
      + `融合评分: <b style="color:var(--blue)">${{n.score}}</b> (${{n.risk_level}})<br/>`
      + `rREI: <b style="color:var(--red)">${{n.rrei}}</b> 次/小时<br/>`
      + `子维度: 事件${{n.event_load}} + 低氧${{n.hypoxia}} + 自主神经${{n.autonomic}}<br/>`
      + `阶段: ${{i < PRE_N ? "干预前·基线期" : i < 25 ? "干预后·调节期" : "干预后·稳定期"}}`;
  }});
}})();

// ═══════════════════════════════════════════════════════════════
// Panel B: 呼吸幅度 + SpO2 双轴
// ═══════════════════════════════════════════════════════════════
(() => {{
  const M = {{ top: 48, right: 60, bottom: 38, left: 54 }};
  const chart = new Chart("panelB", 600, 320, M);
  const ampMin = 0.05, ampMax = 1.55;
  const spo2Min = 78, spo2Max = 100;

  chart.addBackgroundZones();
  chart.drawGrid(ampMin, ampMax, 0.2, true);
  chart.addArea("breath_amp", ampMin, ampMax, "var(--green-fill)");
  chart.addLine("breath_amp", ampMin, ampMax, "var(--green)", false, "呼吸幅度");
  chart.createRightAxis(spo2Min, spo2Max, "最低 SpO₂ (%)", "var(--purple)");
  chart.addLine("spo2_min", spo2Min, spo2Max, "var(--purple)", true, "SpO₂");
  chart.addVLine(PRE_N, "干预开始", "var(--intervention-line)", "6,3");
  chart.addHLine(1.0, ampMin, ampMax, "基线 1.0", "var(--text-muted)", "6,3");
  chart.addHLine(spo2_hypoxia, spo2Min, spo2Max, "低氧阈值 90%", "var(--purple)", "3,4");
  chart.createLeftAxis(ampMin, ampMax, "呼吸幅度 (归一化)", "var(--green)");
  chart.drawAxis(ampMin, ampMax);
  chart.createHoverTooltip("ttB", (i) => {{
    const n = NIGHTS[i];
    return `第${{i+1}}晚 ${{n.date}}<br/>`
      + `呼吸幅度: <b style="color:var(--green)">${{n.breath_amp.toFixed(2)}}</b> (基线=1.0)<br/>`
      + `最低SpO₂: <b style="color:var(--purple)">${{n.spo2_min}}%</b> 平均: ${{n.spo2_avg}}%<br/>`
      + `最低幅度比: ${{n.min_breath_amp_ratio.toFixed(2)}}`;
  }});
}})();

// ═══════════════════════════════════════════════════════════════
// Panel C: 体动
// ═══════════════════════════════════════════════════════════════
(() => {{
  const M = {{ top: 48, right: 20, bottom: 38, left: 54 }};
  const chart = new Chart("panelC", 600, 280, M);
  const motMin = 0, motMax = 65;

  chart.addBackgroundZones();
  chart.drawGrid(motMin, motMax, 10, true);
  chart.addBars("body_motion", motMin, motMax, "var(--orange)", 10);
  chart.addLine("large_motion", motMin, motMax, "var(--red)", false, "大幅体动");
  chart.addVLine(PRE_N, "干预开始", "var(--intervention-line)", "6,3");
  chart.addHLine(THRESH_MOTION_AROUSAL, motMin, motMax, "体动觉醒阈值 30", "var(--red)", "3,4");
  chart.addHLine(THRESH_MOTION_BASELINE, motMin, motMax, "基线采集阈值 15", "var(--text-muted)", "3,4");
  chart.createLeftAxis(motMin, motMax, "次数", "var(--orange)");
  chart.drawAxis(motMin, motMax);
  chart.createHoverTooltip("ttC", (i) => {{
    const n = NIGHTS[i];
    return `第${{i+1}}晚 ${{n.date}}<br/>`
      + `体动次数: <b style="color:var(--orange)">${{n.body_motion}}</b><br/>`
      + `大幅体动: <b style="color:var(--red)">${{n.large_motion}}</b><br/>`
      + `翻身: ${{n.turn_over}} 次`;
  }});
}})();

// ═══════════════════════════════════════════════════════════════
// Panel D: 呼吸事件统计
// ═══════════════════════════════════════════════════════════════
(() => {{
  const M = {{ top: 48, right: 20, bottom: 38, left: 54 }};
  const chart = new Chart("panelD", 600, 280, M);
  const evtMin = 0, evtMax = 90;

  chart.addBackgroundZones();
  chart.drawGrid(evtMin, evtMax, 15, true);
  chart.addBars("hypopnea_like", evtMin, evtMax, "var(--orange)", 10);
  chart.addLine("apnea_like", evtMin, evtMax, "var(--red)", false, "呼吸暂停");
  chart.addVLine(PRE_N, "干预开始", "var(--intervention-line)", "6,3");
  chart.createLeftAxis(evtMin, evtMax, "事件次数", "var(--red)");
  chart.drawAxis(evtMin, evtMax);
  chart.createHoverTooltip("ttD", (i) => {{
    const n = NIGHTS[i];
    return `第${{i+1}}晚 ${{n.date}}<br/>`
      + `疑似呼吸暂停: <b style="color:var(--red)">${{n.apnea_like}}</b><br/>`
      + `疑似低通气: <b style="color:var(--orange)">${{n.hypopnea_like}}</b><br/>`
      + `总计: ${{n.apnea_like + n.hypopnea_like}} · rREI: ${{n.rrei}}<br/>`
      + `鼾声次数: ${{n.snore_count}} · 概率: ${{(n.snore_score*100).toFixed(0)}}%`;
  }});
}})();

// ═══════════════════════════════════════════════════════════════
// Panel E: 枕头调节日志
// ═══════════════════════════════════════════════════════════════
(() => {{
  const M = {{ top: 48, right: 60, bottom: 42, left: 54 }};
  const chart = new Chart("panelE", 1200, 330, M);
  const hMin = 0, hMax = 8.5;
  const aMin = 0, aMax = 16;

  chart.addBackgroundZones();
  chart.drawGrid(hMin, hMax, 1, true);
  chart.addPillowBars(hMin, hMax, "var(--teal)");
  chart.createRightAxis(aMin, aMax, "角度 (°)", "var(--pink)");
  chart.addPillowAngleLine(aMin, aMax, "var(--pink)");
  chart.addPillowTriggerLabels(hMin, hMax);
  chart.addVLine(PRE_N, "干预开始", "var(--intervention-line)", "6,3");
  chart.addHLine(8.0, hMin, hMax, "最大高度 8cm", "var(--teal)", "2,4");
  chart.addHLine(15.0, hMin, hMax, "最大角度 15°", "var(--pink)", "2,4");
  chart.createLeftAxis(hMin, hMax, "高度 (cm)", "var(--teal)");

  // 阶段标签
  const xPreMid = chart.tx(7);
  const lPre = el("text", {{ x: xPreMid, y: M.top - 28, "text-anchor": "middle",
    fill: "var(--intervention-line)", "font-size": "11", "font-weight": "600", opacity: "0.8" }});
  lPre.textContent = "干预前基线期 (15晚)";
  chart.svg.appendChild(lPre);
  const xPostMid = chart.tx(22);
  const lPost = el("text", {{ x: xPostMid, y: M.top - 16, "text-anchor": "middle",
    fill: "var(--green)", "font-size": "11", "font-weight": "600", opacity: "0.8" }});
  lPost.textContent = "干预后改善期 (15晚)";
  chart.svg.appendChild(lPost);

  chart.drawAxis(hMin, hMax);
  chart.createHoverTooltip("ttE", (i) => {{
    const p = PILLOW[i];
    const labels = {{ "snore": "鼾声触发→加高度", "apnea": "呼吸暂停→加角度",
      "hypopnea": "低通气→加角度", "spo2_drop": "SpO₂下降→加高度",
      "motion_high": "体动过多→微调高度", "relax": "指标良好→逐步回落", "none": "无调节" }};
    const t = p.trigger.split("+").map(t => labels[t] || t).join(" + ");
    return `第${{i+1}}晚 ${{p.date}}<br/>`
      + `枕头高度: <b style="color:var(--teal)">${{p.height_cm}}cm</b> (目标: ${{p.height_target}}cm)<br/>`
      + `枕头角度: <b style="color:var(--pink)">${{p.angle_deg}}°</b> (目标: ${{p.angle_target}}°)<br/>`
      + `触发: ${{t}}`;
  }});
}})();

// ── 阈值常量同步 ──
const THRESH_SCORE_MODERATE = 50;
const THRESH_SCORE_MILD = 75;
const THRESH_SPO2_HYPOXIA = 90;
const THRESH_MOTION_AROUSAL = 30;
const THRESH_MOTION_BASELINE = 15;
const spo2_hypoxia = 90;
</script>
</body>
</html>"""


# ═══════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════
def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    # Force UTF-8 on Windows console (Python 3.7+)
    import sys
    if sys.platform == 'win32':
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')

    print("=" * 60)
    print("智能枕头干预前后对比数据生成")
    print("=" * 60)

    nights, pillow_log = generate_all_nights()

    # 打印摘要
    pre = nights[:INTERVENTION_START]
    post = nights[INTERVENTION_START:]

    print(f"\n{'日期':<10} {'阶段':<6} {'评分':>4} {'rREI':>5} {'SpO2min':>7} {'幅度':>5} {'体动':>4} {'鼾声%':>6} {'风险等级':<8}")
    print("-" * 65)
    for n in nights:
        phase_tag = "◀ 干预前" if n["phase"] == "pre" else "  干预后"
        print(f"{n['date']:<10} {phase_tag:<6} {n['score']:>4} {n['rrei']:>5.1f} {n['spo2_min']:>6.1f}% "
              f"{n['breath_amp']:>5.2f} {n['body_motion']:>4} {n['snore_score']*100:>5.0f}% {n['risk_level']:<8}")

    print(f"\n{'枕头调节日志':-^60}")
    for p in pillow_log:
        if p["trigger"] != "none":
            print(f"  {p['date']}  高度={p['height_cm']:.1f}cm  角度={p['angle_deg']:.0f}°  触发={p['trigger']}")

    # 统计
    pre_avg = sum(n["score"] for n in pre) / len(pre)
    post_avg = sum(n["score"] for n in post) / len(post)
    pre_rrei = sum(n["rrei"] for n in pre) / len(pre)
    post_rrei = sum(n["rrei"] for n in post) / len(post)
    print(f"\n{'='*60}")
    print(f"  干预前平均融合评分: {pre_avg:.1f} / 100")
    print(f"  干预后平均融合评分: {post_avg:.1f} / 100  (↑ +{post_avg - pre_avg:.0f})")
    print(f"  干预前平均 rREI: {pre_rrei:.1f}")
    print(f"  干预后平均 rREI: {post_rrei:.1f}  (↓ {((1-post_rrei/max(pre_rrei,0.1))*100):.0f}%)")
    print(f"  最终枕头状态: 高度={pillow_log[-1]['height_cm']:.1f}cm  角度={pillow_log[-1]['angle_deg']:.0f}°")
    print(f"{'='*60}")

    # 生成 HTML
    html = build_html(nights, pillow_log)
    html_path = os.path.join(OUT_DIR, "intervention_comparison.html")
    with open(html_path, "w", encoding="utf-8") as f:
        f.write(html)
    print(f"\n✅ HTML 报告已生成: {html_path}")

    # 保存原始数据 JSON（方便调试修改）
    data_path = os.path.join(OUT_DIR, "intervention_data.json")
    with open(data_path, "w", encoding="utf-8") as f:
        json.dump({"nights": nights, "pillow_log": pillow_log}, f, indent=2, ensure_ascii=False)
    print(f"✅ 原始数据 JSON: {data_path}")


if __name__ == "__main__":
    main()
