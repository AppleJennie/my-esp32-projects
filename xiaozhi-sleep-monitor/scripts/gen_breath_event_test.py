"""
雷达呼吸事件测试图 — 三组典型波形与系统判定结果对比
基于 radar_breath_event.c 的事件分类逻辑
"""
import math, json, os, random

random.seed(2026)

OUT_DIR = os.path.join(os.path.dirname(__file__), "out")
FS = 20          # 生成采样率
DISPLAY_FS = 5   # HTML 显示采样率
DURATION = 60    # 每组 60 秒
BREATH_PERIOD = 4.0  # 15 bpm
BASELINE = 1.0


def breath_wave(t_sec, amp=1.0, noise=0.02):
    """基础呼吸正弦波"""
    phase = (t_sec % BREATH_PERIOD) / BREATH_PERIOD * 2 * math.pi
    w = math.sin(phase)
    w += 0.08 * math.sin(2 * phase + 0.3)  # 谐波
    w += random.gauss(0, noise)
    return amp * w


# ═══════════════════════════════════════════════════════════════
# 场景 1: 正常呼吸
# ═══════════════════════════════════════════════════════════════
def generate_normal():
    N = DURATION * FS
    amp = []
    for i in range(N):
        t = i / FS
        amp.append(BASELINE * breath_wave(t, amp=1.0, noise=0.015))

    events = []  # 无事件
    verdict = {
        "type": "BREATH_EVT_STABLE",
        "label": "呼吸稳定",
        "confidence": 60,
        "reason": "呼吸稳定，幅度在基线附近波动",
        "color": "#10b981",
        "bg": "rgba(16,185,129,0.08)",
    }
    return downsample(amp), events, verdict


# ═══════════════════════════════════════════════════════════════
# 场景 2: 幅度下降 → 暂停 → 恢复
# ═══════════════════════════════════════════════════════════════
def generate_drop():
    N = DURATION * FS
    amp = []

    # 幅度包络: 正常(0-15s) → 变浅(15-30s) → 暂停(30-45s) → 恢复(45-55s) → 正常(55-60s)
    def envelope(t):
        if t < 15:
            return 1.0
        elif t < 30:
            # 渐变浅: 1.0 → 0.3
            return 1.0 - 0.7 * (t - 15) / 15
        elif t < 45:
            # 近暂停: 0.3 → 0.06
            return 0.3 - 0.24 * (t - 30) / 15
        elif t < 55:
            # 恢复大呼吸: 0.06 → 1.5 → 1.3
            mid = 50
            if t < mid:
                return 0.06 + 1.44 * (t - 45) / (mid - 45)
            else:
                return 1.5 - 0.2 * (t - mid) / (55 - mid)
        else:
            return 1.0

    for i in range(N):
        t = i / FS
        env = envelope(t)
        w = breath_wave(t, amp=env, noise=0.02)
        amp.append(BASELINE * w)

    events = [
        {"start": 15, "end": 30, "type": "shallow", "label": "呼吸变浅", "desc": "drop_ratio>0.4 持续15s"},
        {"start": 30, "end": 45, "type": "apnea", "label": "疑似暂停", "desc": "drop_ratio>0.8 持续15s"},
        {"start": 45, "end": 55, "type": "recovery", "label": "恢复呼吸", "desc": "幅度>基线×1.3"},
    ]

    verdict = {
        "type": "BREATH_EVT_SHALLOW → PAUSE_SUSPECTED → RECOVERY",
        "label": "呼吸变浅 → 疑似暂停 → 恢复",
        "confidence": 70,
        "reason": "幅度下降>80%, 持续30s后出现恢复性大呼吸",
        "color": "#ef4444",
        "bg": "rgba(239,68,68,0.06)",
    }
    return downsample(amp), events, verdict


# ═══════════════════════════════════════════════════════════════
# 场景 3: 体动干扰
# ═══════════════════════════════════════════════════════════════
def generate_motion():
    N = DURATION * FS
    amp = []
    motion_vals = []

    def motion_envelope(t):
        if t < 12:
            return 0.0
        elif t < 18:
            return (t - 12) / 6 * 30  # 0→30
        elif t < 30:
            # 30→60→30 的剧烈体动
            mid = 24
            if t < mid:
                return 30 + 30 * (t - 18) / (mid - 18)
            else:
                return 60 - 30 * (t - mid) / (30 - mid)
        elif t < 35:
            return 60 * (1 - (t - 30) / 5)  # 60→0
        else:
            return 0.0

    for i in range(N):
        t = i / FS
        mot = motion_envelope(t)
        motion_vals.append(mot)

        # 呼吸信号 + 体动噪声
        breath = breath_wave(t, amp=1.0, noise=0.015)
        # 体动产生大幅度伪影
        if mot > 0:
            motion_noise = random.gauss(0, mot / 15)  # 标准差随体动增大
            # 体动峰值时完全覆盖呼吸信号
            if mot > 30:
                motion_spike = random.uniform(-mot / 20, mot / 20) * random.choice([1, -1])
                breath = breath * max(0, 1 - (mot - 30) / 40) + motion_spike
            else:
                breath = breath + motion_noise * 0.5
        amp.append(BASELINE * breath)

    events = [
        {"start": 12, "end": 18, "type": "motion_mild", "label": "轻微体动", "desc": "body_motion 0→30"},
        {"start": 18, "end": 30, "type": "motion_heavy", "label": "体动干扰", "desc": "body_motion 30→60, 波形失真"},
        {"start": 30, "end": 35, "type": "recovery", "label": "恢复稳定", "desc": "体动消退, 呼吸恢复"},
    ]

    verdict = {
        "type": "BREATH_EVT_MOVEMENT_ARTIFACT",
        "label": "体动干扰",
        "confidence": 70,
        "reason": "body_motion>30, 呼吸波形被大幅体动覆盖",
        "color": "#f59e0b",
        "bg": "rgba(245,158,11,0.06)",
    }
    return downsample(amp), events, verdict


def downsample(amp_arr):
    step = FS // DISPLAY_FS
    time_arr = [round(i / FS, 1) for i in range(0, len(amp_arr), step)]
    amp_out = [round(amp_arr[i], 4) for i in range(0, len(amp_arr), step)]
    return {"time": time_arr, "amplitude": amp_out}


# ═══════════════════════════════════════════════════════════════
# HTML 生成
# ═══════════════════════════════════════════════════════════════
def build_html(scenarios):
    data_json = json.dumps(scenarios, ensure_ascii=False)

    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>雷达呼吸事件测试图 — 三组波形与系统判定对比</title>
<style>
:root {{
  --surface: #fcfcfb; --surface2: #f5f4f1; --text-primary: #0b0b0b;
  --text-secondary: #52514e; --text-muted: #8b8a85;
  --grid: #e5e4e1; --axis: #c3c2b7;
  --waveform: #2a78d6; --waveform-fill: rgba(42,120,214,0.07);
  --baseline: #8b8a85;
  --apnea-bg: rgba(220,38,38,0.08); --apnea-stroke: #dc2626; --label-apnea: #b91c1c;
  --shallow-bg: rgba(245,158,11,0.08); --shallow-stroke: #f59e0b; --label-shallow: #b45309;
  --recovery-bg: rgba(16,185,129,0.08); --recovery-stroke: #10b981; --label-recovery: #047857;
  --motion-bg: rgba(139,92,246,0.08); --motion-stroke: #8b5cf6; --label-motion: #6d28d9;
  --shadow: 0 1px 3px rgba(0,0,0,0.08);
  --shadow-lg: 0 2px 8px rgba(0,0,0,0.10);
}}
@media (prefers-color-scheme: dark) {{
:root {{
  --surface: #1a1a19; --surface2: #222220; --text-primary: #f0f0ee;
  --text-secondary: #c3c2b7; --text-muted: #6b6a65;
  --grid: #2d2d2b; --axis: #52514e;
  --waveform: #3987e5; --waveform-fill: rgba(57,135,229,0.10);
  --baseline: #6b6a65;
  --apnea-bg: rgba(239,68,68,0.10); --apnea-stroke: #ef4444; --label-apnea: #fca5a5;
  --shallow-bg: rgba(251,191,36,0.10); --shallow-stroke: #fbbf24; --label-shallow: #fcd34d;
  --recovery-bg: rgba(52,211,153,0.10); --recovery-stroke: #34d399; --label-recovery: #6ee7b7;
  --motion-bg: rgba(167,139,250,0.10); --motion-stroke: #a78bfa; --label-motion: #c4b5fd;
  --shadow: 0 1px 3px rgba(0,0,0,0.4);
  --shadow-lg: 0 2px 8px rgba(0,0,0,0.5);
}}}}
* {{ box-sizing: border-box; margin: 0; padding: 0; }}
body {{
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto,
    "Noto Sans SC", "PingFang SC", "Microsoft YaHei", sans-serif;
  background: var(--surface); color: var(--text-primary);
  display: flex; justify-content: center; padding: 24px;
}}
.container {{ max-width: 1300px; width: 100%; }}
h1 {{ font-size: 20px; font-weight: 700; }}
.subtitle {{ color: var(--text-secondary); font-size: 13px; margin: 4px 0 20px; }}
.panels {{ display: grid; grid-template-columns: repeat(3, 1fr); gap: 16px; }}
.card {{
  background: var(--surface); border: 1px solid var(--grid);
  border-radius: 10px; padding: 20px 18px 14px; box-shadow: var(--shadow);
}}
.card-header {{ display: flex; align-items: center; gap: 10px; margin-bottom: 6px; }}
.card-num {{
  width: 26px; height: 26px; border-radius: 50%;
  display: flex; align-items: center; justify-content: center;
  font-size: 13px; font-weight: 700; color: #fff;
  flex-shrink: 0;
}}
.card-title {{ font-size: 14px; font-weight: 600; }}
.verdict-badge {{
  display: inline-block; font-size: 11px; padding: 3px 8px;
  border-radius: 12px; font-weight: 600; margin: 4px 0 10px;
}}
.chart-container {{ position: relative; width: 100%; margin-bottom: 8px; }}
svg {{ display: block; width: 100%; }}
.tooltip {{
  position: absolute; pointer-events: none; opacity: 0;
  background: var(--text-primary); color: var(--surface);
  font-size: 11px; padding: 6px 10px; border-radius: 6px;
  white-space: nowrap; z-index: 10;
  font-family: "SF Mono","Consolas",monospace;
  box-shadow: var(--shadow-lg);
}}
.metrics {{ display: grid; grid-template-columns: repeat(4,1fr); gap: 6px; margin-top: 6px; }}
.metric {{ text-align: center; }}
.metric-label {{ font-size: 10px; color: var(--text-muted); }}
.metric-value {{ font-size: 13px; font-weight: 600; }}
.legend {{ display: flex; gap: 14px; flex-wrap: wrap; font-size: 10px; color: var(--text-secondary); margin-top: 6px; }}
.legend-item {{ display: flex; align-items: center; gap: 4px; }}
.legend-swatch {{ width: 10px; height: 10px; border-radius: 2px; flex-shrink: 0; }}
.footer-note {{ text-align: center; color: var(--text-muted); font-size: 11px; margin-top: 24px; }}
</style>
</head>
<body>
<div class="container">
  <h1>雷达呼吸事件测试图 — 三组典型波形与系统判定结果</h1>
  <p class="subtitle">R60 毫米波雷达 · 60 秒呼吸波形 · 基于 radar_breath_event.c 事件分类逻辑</p>

  <div class="panels" id="panels"></div>

  <div class="legend" style="justify-content:center;margin-top:16px;">
    <div class="legend-item"><span class="legend-swatch" style="background:var(--waveform)"></span>呼吸波形</div>
    <div class="legend-item"><span class="legend-swatch" style="background:var(--shallow-stroke)"></span>呼吸变浅 (drop&gt;0.4)</div>
    <div class="legend-item"><span class="legend-swatch" style="background:var(--apnea-stroke)"></span>疑似暂停 (drop&gt;0.8)</div>
    <div class="legend-item"><span class="legend-swatch" style="background:var(--recovery-stroke)"></span>恢复呼吸</div>
    <div class="legend-item"><span class="legend-swatch" style="background:var(--motion-stroke)"></span>体动干扰 (motion&gt;30)</div>
    <div class="legend-item"><span class="legend-swatch" style="background:var(--baseline)"></span>基线 1.0</div>
  </div>

  <p class="footer-note">
    ⚠️ 本图为模拟演示数据，展示雷达呼吸事件检测算法原理 · drop_ratio = 1 - 当前幅度/基线幅度 · body_motion 来自雷达 composite 0x80 0x03
  </p>
</div>

<script>
const SCENARIOS = {data_json};
const TITLES = ["场景一: 正常呼吸", "场景二: 幅度下降", "场景三: 体动干扰"];
const NUMS = ["1", "2", "3"];
const NUM_COLORS = ["#10b981", "#ef4444", "#f59e0b"];

const W = 400, H = 280;
const M = {{ top: 40, right: 16, bottom: 32, left: 42 }};
const PW = W - M.left - M.right, PH = H - M.top - M.bottom;
const NS = "http://www.w3.org/2000/svg";

function el(tag, attrs = {{}}) {{
  const e = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  return e;
}}

const panelsDiv = document.getElementById("panels");

SCENARIOS.forEach((sc, si) => {{
  const data = sc.data;
  const events = sc.events;
  const verdict = sc.verdict;
  const tMin = 0, tMax = data.time[data.time.length - 1];
  let ampMax = 0, ampMin = 0;
  data.amplitude.forEach(a => {{ if (a > ampMax) ampMax = a; if (a < ampMin) ampMin = a; }});
  ampMax = Math.ceil(Math.max(ampMax, 1.3) * 10) / 10 + 0.15;
  ampMin = Math.floor(Math.min(ampMin, -0.1) * 10) / 10 - 0.1;
  const yMid = (ampMin + ampMax) / 2;
  ampMax = yMid + Math.max(ampMax - yMid, yMid - ampMin) + 0.1;
  ampMin = yMid - (ampMax - yMid);

  function tx(t) {{ return M.left + (t - tMin) / (tMax - tMin) * PW; }}
  function ty(a) {{ return M.top + PH - (a - ampMin) / (ampMax - ampMin) * PH; }}

  // ── Build Card ──
  const card = document.createElement("div"); card.className = "card";

  // Header
  const header = document.createElement("div"); header.className = "card-header";
  const num = document.createElement("span"); num.className = "card-num";
  num.style.background = NUM_COLORS[si]; num.textContent = NUMS[si];
  const title = document.createElement("span"); title.className = "card-title";
  title.textContent = TITLES[si];
  header.appendChild(num); header.appendChild(title); card.appendChild(header);

  // Verdict badge
  const badge = document.createElement("span"); badge.className = "verdict-badge";
  badge.style.background = verdict.bg; badge.style.color = verdict.color;
  badge.textContent = verdict.label + " (置信度 " + verdict.confidence + ")";
  card.appendChild(badge);

  // Chart
  const chartDiv = document.createElement("div"); chartDiv.className = "chart-container";
  const tooltip = document.createElement("div"); tooltip.className = "tooltip";
  tooltip.id = "tt" + si; chartDiv.appendChild(tooltip);

  const svg = el("svg", {{ viewBox: `0 0 ${{W}} ${{H}}`, xmlns: NS, overflow: "visible" }});

  // ── Event zones ──
  const ZC = {{
    apnea: {{ fill: "var(--apnea-bg)", stroke: "var(--apnea-stroke)", label: "var(--label-apnea)" }},
    shallow: {{ fill: "var(--shallow-bg)", stroke: "var(--shallow-stroke)", label: "var(--label-shallow)" }},
    recovery: {{ fill: "var(--recovery-bg)", stroke: "var(--recovery-stroke)", label: "var(--label-recovery)" }},
    motion_mild: {{ fill: "var(--motion-bg)", stroke: "var(--motion-stroke)", label: "var(--label-motion)", opacity: "0.5" }},
    motion_heavy: {{ fill: "var(--motion-bg)", stroke: "var(--motion-stroke)", label: "var(--label-motion)" }},
  }};

  for (const ev of events) {{
    const c = ZC[ev.type];
    const x1 = tx(ev.start), x2 = tx(ev.end), w = x2 - x1;
    svg.appendChild(el("rect", {{ x: x1, y: M.top, width: w, height: PH, fill: c.fill,
      opacity: c.opacity || "1" }}));
    svg.appendChild(el("line", {{ x1, y1: M.top, x2, y2: M.top, stroke: c.stroke,
      "stroke-width": "3", "stroke-linecap": "round", opacity: c.opacity || "0.9" }}));
    const lbl = el("text", {{ x: x1 + w/2, y: M.top - 12, "text-anchor": "middle",
      fill: c.label, "font-size": "9", "font-weight": "600" }});
    lbl.textContent = ev.label;
    svg.appendChild(lbl);
  }}

  // ── Grid ──
  for (let a = 0; a <= ampMax; a += 0.3) {{
    const y = ty(a);
    svg.appendChild(el("line", {{ x1: M.left, y1: y, x2: M.left + PW, y2: y,
      stroke: "var(--grid)", "stroke-width": "1" }}));
    const l = el("text", {{ x: M.left - 6, y: y + 3, "text-anchor": "end",
      fill: "var(--text-secondary)", "font-size": "9" }});
    l.textContent = a.toFixed(1);
    svg.appendChild(l);
  }}
  for (let t = 0; t <= tMax; t += 20) {{
    const x = tx(t);
    svg.appendChild(el("line", {{ x1: x, y1: M.top, x2: x, y2: M.top + PH,
      stroke: t === 0 ? "var(--axis)" : "var(--grid)", "stroke-width": "1" }}));
    const l = el("text", {{ x, y: M.top + PH + 14, "text-anchor": "middle",
      fill: "var(--text-secondary)", "font-size": "9" }});
    l.textContent = t + "s";
    svg.appendChild(l);
  }}

  // ── Baseline ──
  const blY = ty(1.0);
  if (blY > M.top && blY < M.top + PH) {{
    svg.appendChild(el("line", {{ x1: M.left, y1: blY, x2: M.left + PW, y2: blY,
      stroke: "var(--baseline)", "stroke-width": "1.2", "stroke-dasharray": "6,3" }}));
    const blL = el("text", {{ x: M.left + PW - 2, y: blY - 4, "text-anchor": "end",
      fill: "var(--baseline)", "font-size": "9" }});
    blL.textContent = "基线 1.0";
    svg.appendChild(blL);
  }}

  // ── Zero line ──
  const zY = ty(0);
  svg.appendChild(el("line", {{ x1: M.left, y1: zY, x2: M.left + PW, y2: zY,
    stroke: "var(--axis)", "stroke-width": "0.5", opacity: "0.4" }}));

  // ── Waveform ──
  let d = "";
  for (let i = 0; i < data.time.length; i++) {{
    d += `${{i === 0 ? "M" : "L"}}${{tx(data.time[i]).toFixed(1)}},${{ty(data.amplitude[i]).toFixed(1)}} `;
  }}
  const areaD = d + `L${{tx(tMax).toFixed(1)}},${{ty(ampMin)}} L${{tx(tMin).toFixed(1)}},${{ty(ampMin)}} Z`;
  svg.appendChild(el("path", {{ d: areaD, fill: "var(--waveform-fill)", stroke: "none" }}));
  svg.appendChild(el("path", {{ d, fill: "none", stroke: "var(--waveform)", "stroke-width": "1.8",
    "stroke-linejoin": "round", "stroke-linecap": "round" }}));

  // ── Axis box ──
  svg.appendChild(el("rect", {{ x: M.left, y: M.top, width: PW, height: PH,
    fill: "none", stroke: "var(--axis)", "stroke-width": "1" }}));

  // ── Labels ──
  const yLab = el("text", {{ x: 12, y: M.top + PH / 2, "text-anchor": "middle",
    fill: "var(--text-secondary)", "font-size": "10",
    transform: `rotate(-90,12,${{M.top + PH / 2}})` }});
  yLab.textContent = "幅度";
  svg.appendChild(yLab);
  const xLab = el("text", {{ x: M.left + PW / 2, y: H - 4, "text-anchor": "middle",
    fill: "var(--text-secondary)", "font-size": "10" }});
  xLab.textContent = "时间 (秒)";
  svg.appendChild(xLab);

  chartDiv.appendChild(svg);
  card.appendChild(chartDiv);

  // ── Metrics table ──
  const metrics = document.createElement("div"); metrics.className = "metrics";
  const dropRatio = sc.drop_ratio_max != null ? (sc.drop_ratio_max * 100).toFixed(0) + "%" : "~0%";
  const motionMax = sc.motion_max != null ? sc.motion_max.toFixed(0) : "<10";
  const minAmp = Math.min(...data.amplitude).toFixed(2);
  const items = [
    {{ label: "最小幅度", value: minAmp }},
    {{ label: "最大 drop_ratio", value: dropRatio }},
    {{ label: "体动峰值", value: motionMax }},
    {{ label: "判定置信度", value: verdict.confidence + "%" }},
  ];
  items.forEach(it => {{
    const div = document.createElement("div"); div.className = "metric";
    const lbl = document.createElement("div"); lbl.className = "metric-label"; lbl.textContent = it.label;
    const val = document.createElement("div"); val.className = "metric-value"; val.textContent = it.value;
    div.appendChild(lbl); div.appendChild(val); metrics.appendChild(div);
  }});
  card.appendChild(metrics);

  // ── Reason ──
  const reason = document.createElement("div");
  reason.style.cssText = "font-size:10px;color:var(--text-muted);margin-top:6px;line-height:1.4;";
  reason.textContent = "系统判定: " + verdict.reason;
  card.appendChild(reason);

  panelsDiv.appendChild(card);

  // ── Tooltip ──
  svg.addEventListener("mousemove", (e) => {{
    const rect = svg.getBoundingClientRect();
    const scaleX = W / rect.width;
    const svgX = (e.clientX - rect.left) * scaleX;
    let best = 0, bestDist = Infinity;
    for (let i = 0; i < data.time.length; i++) {{
      const dist = Math.abs(tx(data.time[i]) - svgX);
      if (dist < bestDist) {{ bestDist = dist; best = i; }}
    }}
    if (bestDist < 16 * scaleX) {{
      const a = data.amplitude[best], r = a / 1.0;
      let status = "正常";
      if (Math.abs(a) < 0.2) status = "疑似暂停(幅度<0.2)";
      else if (Math.abs(a) < 0.6) status = "呼吸变浅(幅度<0.6)";
      else if (a > 1.3) status = "恢复呼吸(>1.3x基线)";
      tooltip.style.opacity = "1";
      tooltip.style.left = (e.offsetX + 10) + "px";
      tooltip.style.top = (e.offsetY - 40) + "px";
      tooltip.innerHTML = `t=${{data.time[best]}}s 幅度=${{a.toFixed(3)}} 比率=${{r.toFixed(2)}} ${{status}}`;
    }}
  }});
  svg.addEventListener("mouseleave", () => {{ tooltip.style.opacity = "0"; }});
}});
</script>
</body>
</html>"""


# ═══════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════
def main():
    import sys
    if sys.platform == 'win32':
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    os.makedirs(OUT_DIR, exist_ok=True)

    print("=" * 60)
    print("雷达呼吸事件测试图生成")
    print("=" * 60)

    # 场景 1
    print("\n[场景1] 正常呼吸...")
    data1, events1, verdict1 = generate_normal()

    # 场景 2
    print("[场景2] 幅度下降...")
    data2, events2, verdict2 = generate_drop()

    # 场景 3
    print("[场景3] 体动干扰...")
    data3, events3, verdict3 = generate_motion()

    scenarios = [
        {
            "data": data1, "events": events1, "verdict": verdict1,
            "drop_ratio_max": 0.05, "motion_max": None,
        },
        {
            "data": data2, "events": events2, "verdict": verdict2,
            "drop_ratio_max": 0.92, "motion_max": None,
        },
        {
            "data": data3, "events": events3, "verdict": verdict3,
            "drop_ratio_max": None, "motion_max": 60,
        },
    ]

    # 打印摘要
    for i, sc in enumerate(scenarios):
        amp_arr = sc["data"]["amplitude"]
        print(f"\n  场景{i+1}: {sc['verdict']['label']}")
        print(f"    点数: {len(amp_arr)}, 幅度范围: {min(amp_arr):.3f} ~ {max(amp_arr):.3f}")
        print(f"    事件数: {len(sc['events'])}, 判定: {sc['verdict']['type']}")
        print(f"    置信度: {sc['verdict']['confidence']}")
        if sc["events"]:
            for ev in sc["events"]:
                print(f"    [{ev['start']}s-{ev['end']}s] {ev['label']} — {ev['desc']}")

    # 生成 HTML
    html = build_html(scenarios)
    html_path = os.path.join(OUT_DIR, "breath_event_test.html")
    with open(html_path, "w", encoding="utf-8") as f:
        f.write(html)
    print(f"\n✅ HTML 报告已生成: {html_path}")

    # 保存 JSON 数据
    data_path = os.path.join(OUT_DIR, "breath_event_test_data.json")
    with open(data_path, "w", encoding="utf-8") as f:
        json.dump(scenarios, f, indent=2, ensure_ascii=False)
    print(f"✅ 原始数据 JSON: {data_path}")


if __name__ == "__main__":
    main()
