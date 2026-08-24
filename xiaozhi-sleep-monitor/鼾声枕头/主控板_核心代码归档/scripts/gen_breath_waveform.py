"""
生成雷达呼吸波形演示 HTML — 120秒，含正常/低通气/暂停/恢复
"""
import math, json, os, random

random.seed(42)

FS = 20
DURATION = 120
N = DURATION * FS

# 事件时间线: (start, end, type, amp_ratio)
EVENTS = [
    (0,   25,  "normal",    1.0),
    (25,  35,  "hypopnea",  0.35),
    (35,  42,  "apnea",     0.05),
    (42,  48,  "recovery",  1.40),
    (48,  80,  "normal",    1.0),
    (80,  88,  "hypopnea",  0.40),
    (88,  93,  "apnea",     0.04),
    (93,  98,  "recovery",  1.35),
    (98, 120,  "normal",    1.0),
]

BASELINE = 1.0
BREATH_PERIOD = 4.0

amplitude = []
for i in range(N):
    t = i / FS
    phase = (t % BREATH_PERIOD) / BREATH_PERIOD * 2 * math.pi
    wave = math.sin(phase)
    wave += 0.10 * math.sin(2 * phase + 0.2)
    wave += random.gauss(0, 0.02)

    # 找当前段 + 边界过渡
    ratio = 1.0
    for idx, (s, e, etype, r) in enumerate(EVENTS):
        if s <= t < e:
            if t - s < 1.0 and idx > 0:
                prev_r = EVENTS[idx - 1][3]
                b = (t - s) / 1.0
                ratio = prev_r + (r - prev_r) * b
            elif e - t < 1.0 and idx < len(EVENTS) - 1:
                next_r = EVENTS[idx + 1][3]
                b = (e - t) / 1.0
                ratio = r + (next_r - r) * (1.0 - b)
            else:
                ratio = r
            break

    amplitude.append(BASELINE * ratio * max(0.0, wave))

# 降采样到 5Hz
DISPLAY_FS = 5
step = FS // DISPLAY_FS
time_arr = [round(i / FS, 1) for i in range(0, N, step)]
amp_arr = [round(amplitude[i], 4) for i in range(0, N, step)]

# 事件区域
event_zones = [{"start": s, "end": e, "type": tp}
               for (s, e, tp, _) in EVENTS if tp != "normal"]

# ── 生成自包含 HTML ──
html = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>雷达呼吸波形 — 睡眠呼吸暂停检测演示</title>
<style>
:root {{
  --surface: #fcfcfb; --text-primary: #0b0b0b; --text-secondary: #52514e;
  --grid: #e5e4e1; --axis: #c3c2b7;
  --waveform: #2a78d6; --waveform-fill: rgba(42,120,214,0.08);
  --baseline: #8b8a85;
  --apnea-bg: rgba(220,38,38,0.08); --apnea-stroke: #dc2626;
  --hypopnea-bg: rgba(245,158,11,0.08); --hypopnea-stroke: #f59e0b;
  --recovery-bg: rgba(16,185,129,0.08); --recovery-stroke: #10b981;
  --label-apnea: #b91c1c; --label-hypopnea: #b45309; --label-recovery: #047857;
  --shadow: 0 1px 3px rgba(0,0,0,0.08);
}}
@media (prefers-color-scheme: dark) {{
:root {{
  --surface: #1a1a19; --text-primary: #f0f0ee; --text-secondary: #c3c2b7;
  --grid: #2d2d2b; --axis: #52514e;
  --waveform: #3987e5; --waveform-fill: rgba(57,135,229,0.12);
  --baseline: #6b6a65;
  --apnea-bg: rgba(239,68,68,0.10); --apnea-stroke: #ef4444;
  --hypopnea-bg: rgba(251,191,36,0.10); --hypopnea-stroke: #fbbf24;
  --recovery-bg: rgba(52,211,153,0.10); --recovery-stroke: #34d399;
  --label-apnea: #fca5a5; --label-hypopnea: #fcd34d; --label-recovery: #6ee7b7;
  --shadow: 0 1px 3px rgba(0,0,0,0.4);
}}}}
* {{ box-sizing: border-box; margin: 0; padding: 0; }}
body {{
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
  background: var(--surface); color: var(--text-primary);
  display: flex; justify-content: center; align-items: center;
  min-height: 100vh; padding: 24px;
}}
.card {{
  background: var(--surface); border-radius: 12px;
  box-shadow: var(--shadow); border: 1px solid var(--grid);
  padding: 32px 36px 24px; max-width: 1100px; width: 100%;
}}
h2 {{ font-size: 20px; font-weight: 600; margin-bottom: 4px; }}
.subtitle {{ color: var(--text-secondary); font-size: 13px; margin-bottom: 20px; }}
.chart-container {{ position: relative; width: 100%; height: 450px; }}
svg {{ display: block; width: 100%; height: 100%; }}
.legend {{ display: flex; gap: 20px; margin-top: 16px; flex-wrap: wrap; font-size: 12px; color: var(--text-secondary); }}
.legend-item {{ display: flex; align-items: center; gap: 6px; }}
.legend-swatch {{ width: 12px; height: 12px; border-radius: 2px; flex-shrink: 0; }}
.tooltip {{
  position: absolute; pointer-events: none; opacity: 0;
  background: var(--text-primary); color: var(--surface);
  font-size: 12px; padding: 6px 10px; border-radius: 6px;
  white-space: nowrap; z-index: 10;
  font-family: "SF Mono", "Consolas", monospace;
}}
</style>
</head>
<body>
<div class="card">
  <h2>雷达呼吸波形 — 呼吸暂停事件检测演示</h2>
  <p class="subtitle">R60ABD1 毫米波雷达 · 呼吸幅度 120 秒 · 含低通气 / 呼吸暂停 / 恢复呼吸标注</p>
  <div class="chart-container" id="chart"><div class="tooltip" id="tooltip"></div></div>
  <div class="legend">
    <div class="legend-item"><span class="legend-swatch" style="background:var(--waveform)"></span>呼吸波形</div>
    <div class="legend-item"><span class="legend-swatch" style="background:var(--hypopnea-stroke)"></span>低通气 (降幅 60%~65%)</div>
    <div class="legend-item"><span class="legend-swatch" style="background:var(--apnea-stroke)"></span>呼吸暂停 (降幅 ≥90%)</div>
    <div class="legend-item"><span class="legend-swatch" style="background:var(--recovery-stroke)"></span>恢复呼吸 (反弹 35%~40%)</div>
    <div class="legend-item"><span class="legend-swatch" style="background:var(--baseline)"></span>基线幅度 1.0</div>
  </div>
</div>

<script>
const DATA = {{
  time: {json.dumps(time_arr)},
  amplitude: {json.dumps(amp_arr)},
  events: {json.dumps(event_zones, ensure_ascii=False)},
  baseline: 1.0
}};

const W = 1080, H = 450;
const M = {{ top: 52, right: 40, bottom: 48, left: 54 }};
const PW = W - M.left - M.right, PH = H - M.top - M.bottom;

// 数据范围
let ampMax = 0;
for (const a of DATA.amplitude) if (a > ampMax) ampMax = a;
ampMax = Math.ceil(ampMax * 10) / 10 + 0.1;
const yMin = -0.05;
const tMin = 0, tMax = DATA.time[DATA.time.length - 1];

function tx(t) {{ return M.left + (t - tMin) / (tMax - tMin) * PW; }}
function ty(a) {{ return M.top + PH - (a - yMin) / (ampMax - yMin) * PH; }}

const NS = "http://www.w3.org/2000/svg";
function el(tag, attrs = {{}}) {{
  const e = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  return e;
}}

const svg = el("svg", {{ viewBox: `0 0 ${{W}} ${{H}}`, xmlns: NS }});
svg.style.overflow = "visible";

const ZC = {{
  apnea:    {{ fill: "var(--apnea-bg)", stroke: "var(--apnea-stroke)", label: "var(--label-apnea)" }},
  hypopnea: {{ fill: "var(--hypopnea-bg)", stroke: "var(--hypopnea-stroke)", label: "var(--label-hypopnea)" }},
  recovery: {{ fill: "var(--recovery-bg)", stroke: "var(--recovery-stroke)", label: "var(--label-recovery)" }},
}};
const ZN = {{ apnea: "呼吸暂停", hypopnea: "低通气", recovery: "恢复呼吸" }};

// ── Event zones ──
for (const ev of DATA.events) {{
  const c = ZC[ev.type];
  const x1 = tx(ev.start), x2 = tx(ev.end), w = x2 - x1;
  svg.appendChild(el("rect", {{ x: x1, y: M.top, width: w, height: PH, fill: c.fill }}));
  svg.appendChild(el("line", {{ x1, y1: M.top, x2, y2: M.top, stroke: c.stroke, "stroke-width": "3", "stroke-linecap": "round" }}));
  const t = el("text", {{ x: x1 + w/2, y: M.top - 18, "text-anchor": "middle", fill: c.label, "font-size": "12", "font-weight": "600" }});
  t.textContent = ZN[ev.type];
  svg.appendChild(t);
  const d = el("text", {{ x: x1 + w/2, y: M.top - 4, "text-anchor": "middle", fill: c.label, "font-size": "10", opacity: "0.7" }});
  d.textContent = `${{ev.end - ev.start}}秒`;
  svg.appendChild(d);
}}

// ── Drop labels ──
const drops = [
  {{ x: 30, a: 0.33, t: "降幅≈65%", tp: "hypopnea" }},
  {{ x: 38.5, a: 0.12, t: "降幅>90%", tp: "apnea" }},
  {{ x: 45, a: 1.15, t: "反弹+40%", tp: "recovery" }},
  {{ x: 84, a: 0.37, t: "降幅≈60%", tp: "hypopnea" }},
  {{ x: 90.5, a: 0.12, t: "降幅>90%", tp: "apnea" }},
  {{ x: 95.5, a: 1.10, t: "反弹+35%", tp: "recovery" }},
];
for (const dl of drops) {{
  const c = ZC[dl.tp];
  const t = el("text", {{ x: tx(dl.x), y: ty(dl.a) - 6, "text-anchor": "middle",
    fill: c.label, "font-size": "10", "font-weight": "500", opacity: "0.85" }});
  t.textContent = dl.t;
  svg.appendChild(t);
}}

// ── Threshold lines ──
const thresholds = [
  {{ a: 0.2, label: "暂停阈值 (基线×0.2)", color: "var(--apnea-stroke)" }},
  {{ a: 0.6, label: "低通气阈值 (基线×0.6)", color: "var(--hypopnea-stroke)" }},
];
for (const th of thresholds) {{
  const y = ty(th.a);
  svg.appendChild(el("line", {{ x1: M.left, y1: y, x2: M.left + PW, y2: y,
    stroke: th.color, "stroke-width": "1", "stroke-dasharray": "3,4", opacity: "0.5" }}));
  const l = el("text", {{ x: M.left + 6, y: y - 5, fill: th.color, "font-size": "9", opacity: "0.8" }});
  l.textContent = th.label;
  svg.appendChild(l);
}}

// ── Grid ──
for (let a = 0; a <= ampMax; a += 0.2) {{
  const y = ty(a);
  svg.appendChild(el("line", {{ x1: M.left, y1: y, x2: M.left + PW, y2: y, stroke: "var(--grid)", "stroke-width": "1" }}));
  const l = el("text", {{ x: M.left - 8, y: y + 4, "text-anchor": "end", fill: "var(--text-secondary)", "font-size": "10" }});
  l.textContent = a.toFixed(1);
  svg.appendChild(l);
}}
for (let t = 0; t <= tMax; t += 10) {{
  const x = tx(t);
  svg.appendChild(el("line", {{ x1: x, y1: M.top, x2: x, y2: M.top + PH,
    stroke: t === 0 ? "var(--axis)" : "var(--grid)", "stroke-width": "1" }}));
  const l = el("text", {{ x, y: M.top + PH + 16, "text-anchor": "middle", fill: "var(--text-secondary)", "font-size": "10" }});
  l.textContent = `${{t}}s`;
  svg.appendChild(l);
}}

// ── Baseline ──
const blY = ty(1.0);
svg.appendChild(el("line", {{ x1: M.left, y1: blY, x2: M.left + PW, y2: blY,
  stroke: "var(--baseline)", "stroke-width": "1.5", "stroke-dasharray": "6,3" }}));
const blL = el("text", {{ x: M.left + PW - 4, y: blY - 6, "text-anchor": "end", fill: "var(--baseline)", "font-size": "10" }});
blL.textContent = "基线 1.0";
svg.appendChild(blL);

// ── Waveform ──
let d = "";
for (let i = 0; i < DATA.time.length; i++) {{
  d += `${{i === 0 ? "M" : "L"}}${{tx(DATA.time[i]).toFixed(1)}},${{ty(DATA.amplitude[i]).toFixed(1)}} `;
}}
const areaD = d + `L${{tx(tMax).toFixed(1)}},${{ty(yMin)}} L${{tx(tMin).toFixed(1)}},${{ty(yMin)}} Z`;
svg.appendChild(el("path", {{ d: areaD, fill: "var(--waveform-fill)", stroke: "none" }}));
svg.appendChild(el("path", {{ d, fill: "none", stroke: "var(--waveform)", "stroke-width": "2", "stroke-linejoin": "round", "stroke-linecap": "round" }}));

// ── Axis box ──
svg.appendChild(el("rect", {{ x: M.left, y: M.top, width: PW, height: PH, fill: "none", stroke: "var(--axis)", "stroke-width": "1" }}));

// ── Axis labels ──
const yLab = el("text", {{ x: 14, y: M.top + PH / 2, "text-anchor": "middle", fill: "var(--text-secondary)", "font-size": "12",
  transform: `rotate(-90,14,${{M.top + PH / 2}})` }});
yLab.textContent = "呼吸幅度 (归一化)";
svg.appendChild(yLab);
const xLab = el("text", {{ x: M.left + PW / 2, y: H - 6, "text-anchor": "middle", fill: "var(--text-secondary)", "font-size": "12" }});
xLab.textContent = "时间 (秒)";
svg.appendChild(xLab);

// ── Render ──
document.getElementById("chart").appendChild(svg);

// ── Hover tooltip ──
const tooltip = document.getElementById("tooltip");
const chartDiv = document.getElementById("chart");

svg.addEventListener("mousemove", (e) => {{
  const rect = svg.getBoundingClientRect();
  const scaleX = W / rect.width, scaleY = H / rect.height;
  const svgX = (e.clientX - rect.left) * scaleX;
  let best = 0, bestDist = Infinity;
  for (let i = 0; i < DATA.time.length; i++) {{
    const dist = Math.abs(tx(DATA.time[i]) - svgX);
    if (dist < bestDist) {{ bestDist = dist; best = i; }}
  }}
  if (bestDist < 18 * scaleX) {{
    const a = DATA.amplitude[best], r = a / DATA.baseline;
    let status = r <= 0.1 ? "⚠ 呼吸暂停" : r <= 0.6 ? "低通气" : r > 1.3 ? "恢复呼吸" : "正常";
    tooltip.style.opacity = "1";
    tooltip.style.left = (e.clientX - chartDiv.getBoundingClientRect().left + 14) + "px";
    tooltip.style.top = (e.clientY - chartDiv.getBoundingClientRect().top - 40) + "px";
    tooltip.innerHTML = `t=${{DATA.time[best]}}s &nbsp;幅度=${{a.toFixed(3)}} &nbsp;比率=${{r.toFixed(2)}} &nbsp;${{status}}`;
  }}
}});
svg.addEventListener("mouseleave", () => {{ tooltip.style.opacity = "0"; }});
</script>
</body>
</html>"""

out_dir = "d:/embed32/xiaozhi-esp32-main/scripts/out"
os.makedirs(out_dir, exist_ok=True)
out_path = os.path.join(out_dir, "breath_waveform.html")
with open(out_path, "w", encoding="utf-8") as f:
    f.write(html)

# 统计
zones = [z for z in event_zones]
print(f"生成完成: {out_path}")
print(f"数据: {len(time_arr)} 点, {len(zones)} 个事件区")
for z in zones:
    print(f"  [{z['start']:3d}s - {z['end']:3d}s] {z['type']:10s} ({z['end']-z['start']}s)")
print(f"幅度范围: {min(amp_arr):.3f} ~ {max(amp_arr):.3f}")
