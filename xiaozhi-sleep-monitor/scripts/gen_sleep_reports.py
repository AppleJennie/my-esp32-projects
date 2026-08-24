"""
生成 30 天模拟呼吸暂停报告，写入 E:\SLEEP
每个报告模拟真实 sleep_data_center GenerateReportJson 格式
"""
import json
import random
import os
from datetime import datetime, timedelta

random.seed(42)
OUT_DIR = r"E:\SLEEP"

# ── 用户画像：模拟一个轻度~中度 OSA 倾向的中年男性 ──
# 大部分天数正常~轻度，间杂几晚较重

def clamp(v, lo, hi):
    return max(lo, min(hi, v))

def gen_one_day(date_str):
    """生成一天的完整睡眠报告"""

    # ═══════════ Session ═══════════
    sleep_hours = round(random.uniform(5.5, 9.0), 2)
    duration_hours = round(sleep_hours + random.uniform(0.1, 0.5), 2)  # 卧床比睡眠稍长
    data_quality = random.choices(["good", "fair", "poor"], weights=[0.6, 0.3, 0.1])[0]
    has_spo2 = random.choices([True, False], weights=[0.7, 0.3])[0]
    has_radar = True
    has_snore = True
    has_env = random.choices([True, False], weights=[0.8, 0.2])[0]

    # ═══════════ 呼吸事件数据（核心） ═══════════
    # rREI: 0-35，大多数在 3-15
    scenario = random.choices(
        ["normal", "mild", "moderate", "severe"],
        weights=[0.25, 0.40, 0.25, 0.10]
    )[0]

    if scenario == "normal":
        rrei = round(random.uniform(0, 4.9), 1)
        apnea_like_count = random.randint(0, 3)
        hypopnea_like_count = random.randint(0, 8)
    elif scenario == "mild":
        rrei = round(random.uniform(5.0, 14.9), 1)
        apnea_like_count = random.randint(3, 10)
        hypopnea_like_count = random.randint(8, 25)
    elif scenario == "moderate":
        rrei = round(random.uniform(15.0, 29.9), 1)
        apnea_like_count = random.randint(10, 25)
        hypopnea_like_count = random.randint(25, 55)
    else:  # severe
        rrei = round(random.uniform(30.0, 40.0), 1)
        apnea_like_count = random.randint(25, 50)
        hypopnea_like_count = random.randint(50, 80)

    total_event_count = apnea_like_count + hypopnea_like_count
    # 实际 rrei 按公式反推
    rrei = round(total_event_count / sleep_hours, 1)

    # 翻身次数（亚型判定需要，提前生成）
    turn_over_count = random.randint(0, 35)

    # ═══════════ 评分计算 ═══════════
    # A. 事件负荷 (0~50)
    if rrei <= 0:
        event_load_score = 50
    elif rrei <= 5:
        event_load_score = 45
    elif rrei <= 15:
        event_load_score = clamp(45 - int((rrei - 5.0) / 10.0 * 15.0), 30, 45)
    elif rrei <= 30:
        event_load_score = clamp(30 - int((rrei - 15.0) / 15.0 * 20.0), 10, 30)
    else:
        event_load_score = clamp(10 - int((rrei - 30.0) * 0.33), 0, 10)

    # B. 低氧负荷 (0~35)
    if has_spo2:
        min_spo2 = {
            "normal": random.randint(93, 98),
            "mild": random.randint(89, 94),
            "moderate": random.randint(85, 90),
            "severe": random.randint(78, 86),
        }[scenario]
        avg_spo2 = min_spo2 + random.uniform(2.0, 6.0)
        avg_spo2 = round(clamp(avg_spo2, min_spo2, 99), 1)
        t90_ratio = {
            "normal": round(random.uniform(0, 0.01), 3),
            "mild": round(random.uniform(0, 0.05), 3),
            "moderate": round(random.uniform(0.02, 0.12), 3),
            "severe": round(random.uniform(0.08, 0.30), 3),
        }[scenario]
        spo2_below_90_count = int(t90_ratio * duration_hours * 3600 / 30)  # 粗略估计
        spo2_drop_3pct_count = random.randint(0, apnea_like_count * 2)

        if t90_ratio > 0.10:
            hypoxia_score = 0
        elif min_spo2 >= 95:
            hypoxia_score = 35
        elif min_spo2 >= 90:
            hypoxia_score = 28
        elif min_spo2 >= 88:
            hypoxia_score = 18
        elif min_spo2 >= 85:
            hypoxia_score = 10
        else:
            hypoxia_score = clamp(6 - int((85.0 - min_spo2) * 1.2), 0, 6)
    else:
        min_spo2 = 0
        avg_spo2 = 0
        t90_ratio = 0
        spo2_below_90_count = 0
        spo2_drop_3pct_count = 0
        hypoxia_score = 21  # 无血氧中性分

    # C. 自主神经 (0~15)
    has_hr = True
    if scenario in ("moderate", "severe") and random.random() < 0.6:
        # 阻塞型：有心率反跳
        max_delta_hr = round(random.uniform(15, 35), 0)
        no_hr_response_flag = False
    elif scenario in ("mild", "moderate") and random.random() < 0.15:
        # 中枢型：无心率反应
        max_delta_hr = round(random.uniform(1, 5), 0)
        no_hr_response_flag = True
    else:
        max_delta_hr = round(random.uniform(2, 12), 0)
        no_hr_response_flag = False

    if no_hr_response_flag:
        autonomic_score = 0
    elif max_delta_hr <= 10:
        autonomic_score = 15
    elif max_delta_hr <= 20:
        autonomic_score = 10
    else:
        autonomic_score = 5

    total_score = clamp(event_load_score + hypoxia_score + autonomic_score, 0, 100)

    # ═══════════ 风险等级 ═══════════
    if total_score >= 90:
        risk_grade = 0
        risk_level_str = "完美"
    elif total_score >= 75:
        risk_grade = 1
        risk_level_str = "观察级"
    elif total_score >= 50:
        risk_grade = 2
        risk_level_str = "建议关注"
    elif total_score >= 30:
        risk_grade = 3
        risk_level_str = "建议就医"
    else:
        risk_grade = 4
        risk_level_str = "建议尽快评估"

    # 血氧强制规则
    hypoxia_override = False
    if has_spo2:
        if min_spo2 < 85 or t90_ratio > 0.10:
            if risk_grade < 3:
                risk_grade = 3
                risk_level_str = "建议就医"
                hypoxia_override = True
        elif min_spo2 < 90:
            if risk_grade < 4:
                risk_grade = min(risk_grade + 1, 4)
                hypoxia_override = True
                risk_level_str = ["完美","观察级","建议关注","建议就医","建议尽快评估"][risk_grade]

    # ═══════════ 病理标记 ═══════════
    pathological_flag = (rrei >= 5.0) or (has_spo2 and min_spo2 < 90)
    if has_spo2 and apnea_like_count >= 1 and spo2_drop_3pct_count >= 1:
        pathological_flag = True
    autonomic_stress_flag = has_hr and max_delta_hr > 20
    central_pattern_flag = has_hr and no_hr_response_flag and apnea_like_count >= 1

    # ═══════════ 亚型 ═══════════
    if not pathological_flag:
        subtype = 5  # 生理性波动
        subtype_str = "生理性波动"
    elif central_pattern_flag:
        subtype = 4
        subtype_str = "中枢倾向（非医学诊断）"
    elif autonomic_stress_flag and apnea_like_count >= 1:
        subtype = 3
        subtype_str = "阻塞倾向（非医学诊断）"
    elif hypopnea_like_count > apnea_like_count * 3:
        subtype = 2
        subtype_str = "低通气为主型"
    elif turn_over_count > 20 and total_event_count > 0:
        subtype = 1
        subtype_str = "体位加重型疑似"
    else:
        subtype = 6
        subtype_str = "未分类异常"

    # ═══════════ 主原因 ═══════════
    reasons_parts = []
    if not has_spo2:
        reasons_parts.append("未接入血氧，基于雷达呼吸数据评估")
    if rrei >= 15:
        reasons_parts.append(f"呼吸事件频繁(rREI={rrei})")
    elif rrei >= 5:
        reasons_parts.append(f"存在呼吸事件(rREI={rrei})")
    if has_spo2 and min_spo2 < 90:
        reasons_parts.append(f"最低血氧{min_spo2}%")
    if autonomic_stress_flag:
        reasons_parts.append(f"事件后心率明显反跳(ΔHR={max_delta_hr}bpm)")
    if central_pattern_flag:
        reasons_parts.append("呼吸中断时心率无明显代偿，需注意非典型模式")
    main_reason = "，".join(reasons_parts) if reasons_parts else "当前呼吸和血氧指标总体平稳"

    # ═══════════ 建议 ═══════════
    suggestions_list = []
    if risk_grade == 0:
        suggestions_list.append("呼吸状态良好，请保持规律作息。")
    elif risk_grade == 1:
        suggestions_list.append("存在轻微呼吸波动，建议连续观察3晚趋势。")
        if subtype == 1:
            suggestions_list.append("可尝试调整睡姿。")
    elif risk_grade == 2:
        suggestions_list.append("建议关注呼吸和血氧变化，连续监测观察趋势。")
        if subtype == 2:
            suggestions_list.append("注意卧室通风和睡眠姿势。")
        if not has_spo2:
            suggestions_list.append("建议接入血氧设备以提高评估准确性。")
        suggestions_list.append("如长期出现，建议咨询医生。")
    elif risk_grade == 3:
        suggestions_list.append("检测到较强呼吸风险信号，建议进行专业睡眠评估。")
        if has_spo2 and min_spo2 < 88:
            suggestions_list.append("夜间血氧偏低需重点关注。")
    else:
        suggestions_list.append("检测到极高呼吸风险信号，建议尽快进行医学评估。")
    if central_pattern_flag:
        suggestions_list.append("检测到非典型呼吸中断模式，建议进行专业多导睡眠监测(PSG)。")
    suggestions_list.append("建议连续监测多晚观察趋势。")

    main_suggestion = "；".join(suggestions_list[:3])

    # ═══════════ 置信度 ═══════════
    if has_spo2 and has_hr:
        confidence = "medium"
    else:
        confidence = "low"

    # ═══════════ 生命体征 ═══════════
    avg_hr = round(random.uniform(55, 78), 1)
    max_hr = round(avg_hr + random.uniform(15, 40), 0)
    min_hr = round(avg_hr - random.uniform(8, 20), 0)
    avg_br = round(random.uniform(12, 18), 1)
    max_br = round(avg_br + random.uniform(5, 12), 0)
    min_br = round(avg_br - random.uniform(3, 8), 0)
    if min_br < 8:
        min_br = round(random.uniform(8, 10), 0)

    # ═══════════ 体动 ═══════════
    motion_count = random.randint(30, 200)
    large_motion_count = int(motion_count * random.uniform(0.05, 0.25))
    body_absent_count = random.randint(0, 10)
    total_radar_samples = motion_count + body_absent_count
    body_present_rate = round(1.0 - body_absent_count / max(total_radar_samples, 1), 3)

    # ═══════════ 鼾声 ═══════════
    snore_count = random.randint(0, 60)
    snore_total_sec = round(snore_count * random.uniform(3, 45), 0)
    snore_index = round(snore_count / max(sleep_hours, 0.1), 1)
    longest_snore_sec = round(random.uniform(2, max(5, snore_total_sec * 0.3)), 0)
    max_snore_prob = round(random.uniform(0.3, 0.98), 2)
    avg_snore_prob = round(max_snore_prob * random.uniform(0.4, 0.8), 2)

    # ═══════════ 环境 ═══════════
    env_data_available = has_env
    avg_temp = round(random.uniform(18.0, 28.0), 1)
    avg_humi = round(random.uniform(35.0, 65.0), 1)
    avg_light = round(random.uniform(0, 300), 0)
    max_light = avg_light + random.randint(0, 200)

    # 舒适度
    comfort = "good"
    comfort_reasons = []
    comfort_suggestions = []
    if avg_temp < 18:
        comfort = "fair"; comfort_reasons.append("温度偏低"); comfort_suggestions.append("建议适当保暖")
    elif avg_temp > 26:
        comfort = "fair"; comfort_reasons.append("温度偏高"); comfort_suggestions.append("建议适当通风或降低室温")
    if avg_humi < 40:
        comfort = "fair"; comfort_reasons.append("湿度偏低"); comfort_suggestions.append("建议适当加湿")
    elif avg_humi > 60:
        comfort = "fair"; comfort_reasons.append("湿度偏高"); comfort_suggestions.append("建议保持通风")
    if avg_light > 500:
        comfort = "poor"; comfort_reasons.append("环境光偏高，可能影响睡眠"); comfort_suggestions.append("建议关闭灯光或使用遮光窗帘")

    # ═══════════ 音频 ═══════════
    avg_audio_db = round(random.uniform(25, 50), 1)
    max_audio_db = round(avg_audio_db + random.uniform(10, 35), 1)
    vad_count = random.randint(0, 20)

    # ═══════════ 睡眠评分 ═══════════
    sleep_score = clamp(total_score + random.randint(-10, 10), 0, 100)
    if sleep_score >= 85:
        sleep_risk_level = "低风险"
    elif sleep_score >= 65:
        sleep_risk_level = "中等风险"
    else:
        sleep_risk_level = "高风险"

    # ═══════════ 雷达自带上报 ═══════════
    radar_apnea_10min = random.randint(0, max(1, apnea_like_count // 3))

    # ═══════════ 波形特征 ═══════════
    breath_cv = round(random.uniform(0.05, 0.35), 3)
    breath_wave_quality = round(random.uniform(0.5, 1.0), 2)
    hr_sdnn_ms = round(random.uniform(20, 80), 1)
    hrv_quality = round(random.uniform(0.4, 1.0), 2)

    # ═══════════ 构建 JSON ═══════════
    report = {
        "report_type": "sleep_monitor_report",
        "version": "2.0",
        "session": {
            "duration_hours": duration_hours,
            "valid_sleep_hours": sleep_hours,
            "data_quality": data_quality
        },
        "summary": {
            "sleep_score": sleep_score,
            "sleep_risk_level": sleep_risk_level,
            "snore_resp_score": total_score,
            "snore_resp_risk_level": risk_level_str,
            "data_quality": data_quality,
        },
        "snore": {
            "data_available": has_snore,
            "snore_count": snore_count,
            "snore_total_minutes": round(snore_total_sec / 60.0, 1),
            "snore_index": snore_index,
            "longest_snore_seconds": int(longest_snore_sec),
            "max_snore_prob": max_snore_prob,
            "avg_snore_prob": avg_snore_prob
        },
        "vitals": {
            "data_available": True,
            "avg_heart_rate": avg_hr,
            "max_heart_rate": int(max_hr),
            "min_heart_rate": int(min_hr),
            "avg_breath_rate": avg_br,
            "max_breath_rate": int(max_br),
            "min_breath_rate": int(min_br),
            "avg_spo2": avg_spo2 if has_spo2 else None,
            "min_spo2": min_spo2 if has_spo2 else None,
            "spo2_available": has_spo2
        },
        "snore_resp_risk": {
            "score": total_score,
            "risk_level": risk_level_str,
            "pathological_flag": pathological_flag,
            "subtype": subtype_str,
            "rrei": rrei,
            "event_count": total_event_count,
            "apnea_like_count": apnea_like_count,
            "hypopnea_like_count": hypopnea_like_count,
            "event_load_score": event_load_score,
            "hypoxia_score": hypoxia_score,
            "autonomic_score": autonomic_score,
            "spo2_available": has_spo2,
            "min_spo2": min_spo2 if has_spo2 else None,
            "avg_spo2": round(avg_spo2, 1) if has_spo2 else None,
            "t90_ratio": t90_ratio,
            "spo2_below_90_count": spo2_below_90_count,
            "spo2_drop_3pct_count": spo2_drop_3pct_count,
            "max_delta_hr_after_event": max_delta_hr,
            "autonomic_stress_flag": autonomic_stress_flag,
            "central_pattern_flag": central_pattern_flag,
            "confidence": confidence,
            "basis": None if has_spo2 else "未接入血氧数据，基于雷达呼吸波形进行有限评估",
            "main_reason": main_reason,
            "suggestions": suggestions_list[:4],
            "disclaimer": "本结果仅作为家庭睡眠观察参考，不能替代医学诊断。"
        },
        "movement": {
            "data_available": has_radar,
            "body_present_rate": body_present_rate,
            "motion_count": motion_count,
            "large_motion_count": large_motion_count
        },
        "environment": {
            "data_available": env_data_available,
            "avg_temp": avg_temp,
            "avg_humi": avg_humi,
            "avg_light_raw": avg_light,
            "max_light_raw": int(max_light),
            "comfort_level": comfort,
            "comfort_reasons": comfort_reasons,
            "suggestions": comfort_suggestions
        },
        "audio_summary": {
            "avg_audio_db": avg_audio_db,
            "max_audio_db": max_audio_db,
            "vad_count": vad_count
        },
        "score": {
            "sleep_score": sleep_score,
            "risk_level": sleep_risk_level,
            "risk_reasons": [main_reason] if pathological_flag else [],
            "suggestions": suggestions_list[:4],
            "snore_resp_score": total_score,
            "snore_resp_risk_grade": risk_level_str,
            "resp_pathological_flag": pathological_flag
        },
        "data_completeness": {
            "radar": has_radar,
            "audio": has_snore,
            "spo2": has_spo2,
            "environment": has_env,
            "baseline": True
        },
        "disclaimer": "本报告仅作为家庭睡眠观察参考，不能替代医学诊断。"
    }

    if not has_spo2:
        report["summary"]["data_limit"] = "未接入血氧数据，呼吸暂停风险评估可信度有限"
    if pathological_flag:
        report["summary"]["pathological_risk_detected"] = True
    if central_pattern_flag:
        report["score"]["risk_reasons"].append("非典型呼吸中断模式，建议进一步评估")

    return report


# ═══════════ 生成 30 天 ═══════════
end_date = datetime(2026, 7, 6)
start_date = end_date - timedelta(days=29)

print(f"生成 30 天报告: {start_date.strftime('%Y-%m-%d')} → {end_date.strftime('%Y-%m-%d')}")
print(f"输出目录: {OUT_DIR}")
print("-" * 60)

all_reports = []

for i in range(30):
    date = start_date + timedelta(days=i)
    date_str = date.strftime("%Y-%m-%d")
    session_time = f"{random.randint(21, 23):02d}-{random.randint(0, 59):02d}-{random.randint(0, 59):02d}"

    # 创建目录
    session_dir = os.path.join(OUT_DIR, date_str, f"session_{session_time}")
    os.makedirs(session_dir, exist_ok=True)

    report = gen_one_day(date_str)

    # 写 report.json
    json_path = os.path.join(session_dir, "report.json")
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)

    # 写 report.txt（人类可读）
    txt_path = os.path.join(session_dir, "report.txt")
    with open(txt_path, "w", encoding="utf-8") as f:
        r = report
        sr = r["snore_resp_risk"]
        f.write(f"═══════════════════════════════════════\n")
        f.write(f"  睡眠监测报告 — {date_str}\n")
        f.write(f"═══════════════════════════════════════\n\n")
        f.write(f"【基本信息】\n")
        f.write(f"  睡眠时长: {r['session']['valid_sleep_hours']} 小时\n")
        f.write(f"  数据质量: {r['session']['data_quality']}\n\n")
        f.write(f"【呼吸暂停风险评估】\n")
        f.write(f"  综合评分: {sr['score']}/100 → {sr['risk_level']}\n")
        f.write(f"  rREI: {sr['rrei']} 次/小时\n")
        f.write(f"  疑似呼吸暂停: {sr['apnea_like_count']} 次\n")
        f.write(f"  疑似低通气: {sr['hypopnea_like_count']} 次\n")
        f.write(f"  亚型: {sr['subtype']}\n")
        f.write(f"  病理性风险: {'是' if sr['pathological_flag'] else '否'}\n")
        f.write(f"  中枢特征: {'是' if sr['central_pattern_flag'] else '否'}\n")
        f.write(f"  阻塞应激: {'是' if sr['autonomic_stress_flag'] else '否'}\n")
        f.write(f"  子维度: 事件负荷={sr['event_load_score']}/50  ")
        f.write(f"低氧={sr['hypoxia_score']}/35  ")
        f.write(f"自主神经={sr['autonomic_score']}/15\n")
        if sr['spo2_available']:
            f.write(f"  最低血氧: {sr['min_spo2']}%  T90={sr['t90_ratio']}\n")
        else:
            f.write(f"  血氧: 未接入\n")
        f.write(f"  可信度: {sr['confidence']}\n")
        f.write(f"  原因: {sr['main_reason']}\n")
        f.write(f"  心率反跳: ΔHR={sr['max_delta_hr_after_event']}bpm\n\n")
        f.write(f"【生命体征】\n")
        v = r['vitals']
        f.write(f"  心率: 平均{v['avg_heart_rate']} 最低{v['min_heart_rate']} 最高{v['max_heart_rate']}\n")
        f.write(f"  呼吸率: 平均{v['avg_breath_rate']} 最低{v['min_breath_rate']} 最高{v['max_breath_rate']}\n")
        if v['spo2_available']:
            f.write(f"  血氧: 平均{v['avg_spo2']}% 最低{v['min_spo2']}%\n")
        f.write(f"\n【鼾声】\n")
        sn = r['snore']
        f.write(f"  次数: {sn['snore_count']}  总时长: {sn['snore_total_minutes']}分钟\n")
        f.write(f"  指数: {sn['snore_index']}次/小时  最长: {sn['longest_snore_seconds']}秒\n")
        f.write(f"\n【体动】\n")
        mv = r['movement']
        f.write(f"  体动次数: {mv['motion_count']}  大幅体动: {mv['large_motion_count']}\n")
        f.write(f"\n【环境】\n")
        env = r['environment']
        f.write(f"  温度: {env['avg_temp']}°C  湿度: {env['avg_humi']}%  光照: {env['avg_light_raw']}\n")
        f.write(f"  舒适度: {env['comfort_level']}\n")
        f.write(f"\n【建议】\n")
        for s in sr['suggestions']:
            f.write(f"  • {s}\n")
        f.write(f"\n【睡眠评分】: {r['score']['sleep_score']}/100 ({r['score']['risk_level']})\n")
        f.write(f"\n⚠️ 免责: {r['disclaimer']}\n")

    all_reports.append({
        "date": date_str,
        "score": report["snore_resp_risk"]["score"],
        "risk_level": report["snore_resp_risk"]["risk_level"],
        "rrei": report["snore_resp_risk"]["rrei"],
        "apnea": report["snore_resp_risk"]["apnea_like_count"],
        "hypopnea": report["snore_resp_risk"]["hypopnea_like_count"],
        "subtype": report["snore_resp_risk"]["subtype"],
        "pathological": report["snore_resp_risk"]["pathological_flag"],
        "central": report["snore_resp_risk"]["central_pattern_flag"],
    })

    # 简短日志
    sr = report["snore_resp_risk"]
    flag = "!!" if sr["pathological_flag"] else "OK"
    print(f"  {date_str}  score={sr['score']:3d}  {sr['risk_level']:5s}  "
          f"rREI={sr['rrei']:4.1f}  apnea={sr['apnea_like_count']:2d}  hypo={sr['hypopnea_like_count']:2d}  "
          f"{sr['subtype']}  {flag}")

# ═══════════ 汇总索引 ═══════════
print("-" * 60)
print(f"共生成 30 天报告到 {OUT_DIR}")

# 写汇总 CSV
summary_path = os.path.join(OUT_DIR, "_summary.csv")
with open(summary_path, "w", encoding="utf-8") as f:
    f.write("date,score,risk_level,rrei,apnea_count,hypopnea_count,subtype,pathological,central\n")
    for r in all_reports:
        f.write(f"{r['date']},{r['score']},{r['risk_level']},{r['rrei']},"
                f"{r['apnea']},{r['hypopnea']},{r['subtype']},"
                f"{r['pathological']},{r['central']}\n")
print(f"汇总 CSV: {summary_path}")

# 统计
scores = [r['score'] for r in all_reports]
rreis = [r['rrei'] for r in all_reports]
patho_days = sum(1 for r in all_reports if r['pathological'])
central_days = sum(1 for r in all_reports if r['central'])
print(f"\n30天统计:")
print(f"  平均评分: {sum(scores)/len(scores):.1f}")
print(f"  平均rREI: {sum(rreis)/len(rreis):.1f}")
print(f"  病理性风险天数: {patho_days}/30")
print(f"  疑似中枢特征天数: {central_days}/30")
