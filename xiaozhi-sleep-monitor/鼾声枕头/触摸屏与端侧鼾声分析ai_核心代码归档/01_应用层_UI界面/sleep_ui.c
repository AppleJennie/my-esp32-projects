/**
 * @file    sleep_ui.c
 * @brief   AI 睡眠床头监护仪 — 800x480 LVGL v8.3
 *
 *  布局：状态栏(40) + 左卡片/右卡片(240) + 六宫格入口(160) + 底边距
 *  字体：lv_font_sleep_ui_16，覆盖本项目全部中文和标点
 */

#include "sleep_ui.h"
#include "sleep_data.h"
#include "control_panel.h"
#include "hardware_control.h"
#include "sleep_player_ui.h"
#include "sleep_assistant_ui.h"
#include "sleep_project_config.h"
#include "main_report_rx.h"
#if CONFIG_ENABLE_R60_RADAR
#include "sleep_radar_data.h"
#endif
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LV_FONT_DECLARE(lv_font_sleep_ui_16);
LV_FONT_DECLARE(ui_font_AlimamaShuHeiTiFont16Bpp4);
LV_FONT_DECLARE(lv_font_montserrat_48);
LV_FONT_DECLARE(lv_font_montserrat_20);

/* ==================== 布局常量 ==================== */
#define LCD_W 800
#define LCD_H 480
#define B_H   40
#define PAD   24
#define CR    16
#define GAP   16

/* ==================== 主题色 ==================== */
#define C_BG       lv_color_hex(0x0D1117)
#define C_CARD     lv_color_hex(0x161B22)
#define C_CARD2    lv_color_hex(0x1C2128)
#define C_BORDER   lv_color_hex(0x30363D)
#define C_TXT      lv_color_hex(0xE6EDF3)
#define C_SUB      lv_color_hex(0x8B949E)
#define C_DIM      lv_color_hex(0x484F58)
#define C_ON       lv_color_hex(0x3FB950)
#define C_OFF      lv_color_hex(0xF85149)
#define C_WARN     lv_color_hex(0xF0883E)
#define C_BLUE     lv_color_hex(0x58A6FF)
#define C_CYAN     lv_color_hex(0x39C5CF)
#define C_GREEN    lv_color_hex(0x3FB950)
#define C_ORANGE   lv_color_hex(0xF0883E)
#define C_RED      lv_color_hex(0xF85149)
#define C_PURPLE   lv_color_hex(0xA371F7)

#define F_CN       &lv_font_sleep_ui_16
#define F_EXT      &ui_font_AlimamaShuHeiTiFont16Bpp4
#define F48        &lv_font_montserrat_48
#define F20        &lv_font_montserrat_20
#define WAVE_POINTS 60

/* ==================== 页面ID ==================== */
#define PG_HOME    0
#define PG_MONITOR 1
#define PG_WAVE    2
#define PG_REPORT  3
#define PG_EVENT   4
#define PG_AI      5
#define PG_SETTING 6
#define PG_CNT     7

/* ==================== 全局对象 ==================== */
static lv_obj_t *scr;
static lv_obj_t *pages[PG_CNT];

/* 状态栏 */
static lv_obj_t *sb_time, *sb_state, *sb_wifi, *sb_radar, *sb_batt;

/* 首页左卡 */
static lv_obj_t *home_arc;
static lv_obj_t *home_score_val;
static lv_obj_t *home_risk_val;
static lv_obj_t *home_capsule_btn;
static lv_obj_t *home_capsule_lbl;

/* 首页右卡 — OV-Watch 2x2 风格 */
static lv_obj_t *home_dash_title, *home_dash_detail;
static lv_obj_t *home_card_hr, *home_card_br, *home_card_sn, *home_card_stage;
static lv_obj_t *home_audio_btn;

/* 六宫格 */
static lv_obj_t *entry_btn[6];
static lv_obj_t *entry_lbl[6];

/* 设置页体 */
static lv_obj_t *setting_body;

/* 子页动态对象 */
static lv_obj_t *monitor_metric_val[4];
static lv_obj_t *monitor_info_val[5];
static lv_obj_t *wave_chart[3];
static lv_chart_series_t *wave_ser[3];
static lv_obj_t *wave_status_lbl;
static lv_obj_t *report_score_lbl, *report_risk_lbl, *report_eval_lbl;
static lv_obj_t *report_metric_lbl[9];
static lv_obj_t *event_summary_lbl[3], *event_record_lbl;

/* 操作菜单 */
static lv_obj_t *act_menu;
/* 确认弹窗 */
static lv_obj_t *cfm_dlg, *cfm_txt;
static void (*cfm_fn)(void);

static int cur_page = PG_HOME;
static bool mon = false, dnd = false, mic = true, radar = true, mute = false, keep = false;
static char time_str[16] = "00:00:00";

/* ==================== 工具 ==================== */
static lv_obj_t *L(lv_obj_t *p, const char *t, lv_color_t c, const lv_font_t *f) {
    lv_obj_t *l = lv_label_create(p); lv_label_set_text(l, t);
    lv_obj_set_style_text_color(l, c, 0); lv_obj_set_style_text_font(l, f, 0); return l;
}
static lv_obj_t *C(lv_obj_t *p, lv_coord_t w, lv_coord_t h) {
    lv_obj_t *c = lv_obj_create(p); lv_obj_set_size(c, w, h); lv_obj_set_style_radius(c, CR, 0);
    lv_obj_set_style_bg_color(c, C_CARD, 0); lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, C_BORDER, 0); lv_obj_set_style_shadow_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0); lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE); return c;
}
static lv_obj_t *B(lv_obj_t *p, lv_coord_t w, lv_coord_t h, lv_color_t bg) {
    lv_obj_t *b = lv_btn_create(p); lv_obj_set_size(b, w, h); lv_obj_set_style_radius(b, CR, 0);
    lv_obj_set_style_bg_color(b, bg, 0); lv_obj_set_style_bg_color(b, lv_color_darken(bg, LV_OPA_20), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b, 0, 0); lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0); lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE); return b;
}
static void SCB(lv_obj_t *o, lv_event_cb_t cb, lv_event_code_t e) {
    if (o && lv_obj_is_valid(o)) lv_obj_add_event_cb(o, cb, e, NULL);
    else ESP_LOGE("ui", "SCB null");
}

/* ==================== 格式化 ==================== */
static const char *S_s(SleepStage_t s) {
    switch(s){case SLEEP_STAGE_AWAKE:return"清醒";case SLEEP_STAGE_LIGHT:return"浅睡";case SLEEP_STAGE_DEEP:return"深睡";case SLEEP_STAGE_AWAY:return"离床";default:return"--";}
}
static lv_color_t S_c(SleepStage_t s) {
    switch(s){case SLEEP_STAGE_AWAKE:return C_ORANGE;case SLEEP_STAGE_LIGHT:return C_CYAN;case SLEEP_STAGE_DEEP:return C_BLUE;case SLEEP_STAGE_AWAY:return C_SUB;default:return C_DIM;}
}
static const char *P_s(SleepPosture_t p) {
    switch(p){case POSTURE_SUPINE:return"仰卧";case POSTURE_LEFT:return"左侧卧";case POSTURE_RIGHT:return"右侧卧";case POSTURE_PRONE:return"俯卧";default:return"--";}
}
/* ── 鼾声分类中文标签 (snore_audio_analyzer 输出) ── */
static const char *ST_s(int snore_type) {
    switch(snore_type){case 1:return"鼻型";case 2:return"喉型";case 3:return"口型";case 4:return"混型";case 5:return"未知";default:return"无";}
}
static lv_color_t ST_c(int snore_type) {
    switch(snore_type){case 1:return C_CYAN;case 2:return C_ORANGE;case 3:return C_PURPLE;case 4:return C_WARN;case 5:return C_DIM;default:return C_DIM;}
}
static bool D_pathological(const SleepData_t *d) {
    return d->ahi>=5.0f || d->apnea_count>0 ||
           (d->apnea_duration_sec>=10 && d->spo2_drop_percent>=3);
}
static const char *D_class(const SleepData_t *d) {
    return D_pathological(d)?"病理性鼾声":"生理性鼾声";
}
static const char *D_subtype(const SleepData_t *d) {
    if(!D_pathological(d)){
        if(d->snore_count>=3)return"间歇/单纯型";
        if(d->posture==POSTURE_SUPINE&&d->snore_db>=50)return"体位依赖型";
        return"持续性良性";
    }
    if(d->airflow_reduction_percent>=90&&d->delta_hr<=2)return"中枢/混合型";
    if(d->supine_event_percent>90)return"体位加重型";
    if(d->airflow_reduction_percent>=30&&d->airflow_reduction_percent<90)return"低通气为主型";
    return"重度阻塞型";
}
static const char *D_event(const SleepData_t *d) {
    if(!d->apnea_active)return"呼吸稳定";
    if(d->apnea_duration_sec<10)return"疑似低通气";
    if(d->airflow_reduction_percent>=90&&d->delta_hr<=2)return"中枢/混合暂停";
    if(d->airflow_reduction_percent>=90)return"阻塞性暂停";
    return"低通气事件";
}
static int D_risk_level(const SleepData_t *d) {
    int level=d->ahi<5.0f?0:(d->ahi<15.0f?1:(d->ahi<30.0f?2:3));
    if(d->min_spo2<90&&level<3)level++;
    return level;
}
static const char *D_risk(const SleepData_t *d) {
    static const char *names[]={"低","中","高","极高"};
    return names[D_risk_level(d)];
}
static lv_color_t D_risk_color(const SleepData_t *d) {
    int level=D_risk_level(d);
    return level==0?C_GREEN:(level==1?C_WARN:C_RED);
}
static const char *D_advice(const SleepData_t *d) {
    if(!D_pathological(d))return"当前风险较低，建议完成整晚监测以建立个人基线。";
    if(d->airflow_reduction_percent>=90&&d->delta_hr<=2)return"出现中枢/混合特征，建议尽快进行专业睡眠监测。";
    if(d->supine_event_percent>90)return"事件多见于仰卧，建议优先侧卧并持续观察血氧。";
    if(d->min_spo2<90||d->ahi>=15.0f)return"呼吸风险较高，建议尽快就医评估。";
    return"建议继续观察，若反复憋醒或白天困倦请及时就医。";
}
static const char *Y_s(SystemState_t s) {
    switch(s){case SYS_STATE_STANDBY:return"AI待机";case SYS_STATE_AUDIO_ONLY:return"仅音频";case SYS_STATE_MONITORING:return"监护中";case SYS_STATE_SLEEPING:return"睡眠中";case SYS_STATE_REPORT_READY:return"报告就绪";default:return"--";}
}
static lv_color_t Y_c(SystemState_t s) {
    switch(s){case SYS_STATE_STANDBY:return C_SUB;case SYS_STATE_AUDIO_ONLY:return C_ORANGE;case SYS_STATE_MONITORING:return C_BLUE;case SYS_STATE_SLEEPING:return C_PURPLE;case SYS_STATE_REPORT_READY:return C_GREEN;default:return C_DIM;}
}
static void F_d(char *b, size_t l, uint32_t s) {
    snprintf(b,l,"%02lu:%02lu:%02lu",(unsigned long)(s/3600),(unsigned long)((s%3600)/60),(unsigned long)(s%60));
}
static const char *A_s(uint32_t t) {
    static char b[16]; if(t==0)return"--";
    uint32_t a=((uint32_t)(esp_timer_get_time()/1000))-t;
    if(a<3000) return "刚刚";
    snprintf(b,sizeof(b),"%lus",(unsigned long)(a/1000));
    return b;
}
static int CL(int v,int lo,int hi){return v<lo?lo:(v>hi?hi:v);}
static bool fresh_ms(uint32_t t,uint32_t age){if(t==0)return false;uint32_t n=(uint32_t)(esp_timer_get_time()/1000);return (n-t)<=age;}

static lv_obj_t *mk_chart_card(lv_obj_t *bd,const char *ttl,lv_color_t color,int ymin,int ymax,
                               lv_obj_t **out_chart,lv_chart_series_t **out_ser){
    lv_obj_t*s=C(bd,LV_PCT(100),115);
    L(s,ttl,C_SUB,F_CN);lv_obj_set_pos(lv_obj_get_child(s,0),14,8);
    lv_obj_t*ca=lv_chart_create(s);lv_obj_set_size(ca,LV_PCT(100)-28,76);lv_obj_set_pos(ca,14,32);
    lv_obj_set_style_bg_color(ca,C_CARD2,0);lv_obj_set_style_border_width(ca,1,0);
    lv_obj_set_style_border_color(ca,C_BORDER,0);lv_obj_set_style_line_width(ca,1,LV_PART_MAIN);
    lv_obj_set_style_line_color(ca,C_BORDER,LV_PART_MAIN);lv_obj_set_style_size(ca,0,LV_PART_INDICATOR);
    lv_obj_clear_flag(ca,LV_OBJ_FLAG_SCROLLABLE);
    lv_chart_set_type(ca,LV_CHART_TYPE_LINE);lv_chart_set_point_count(ca,WAVE_POINTS);
    lv_chart_set_update_mode(ca,LV_CHART_UPDATE_MODE_SHIFT);lv_chart_set_range(ca,LV_CHART_AXIS_PRIMARY_Y,ymin,ymax);
    lv_chart_set_div_line_count(ca,3,6);
    lv_chart_series_t*ser=lv_chart_add_series(ca,color,LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(ca,ser,0);lv_chart_refresh(ca);
    if(out_chart)*out_chart=ca;
    if(out_ser)*out_ser=ser;
    return s;
}

/* ==================== 状态栏 ==================== */
static void mk_bar(lv_obj_t *p) {
    lv_obj_t *r = lv_obj_create(p); lv_obj_set_size(r, LCD_W, B_H); lv_obj_set_pos(r,0,0);
    lv_obj_set_style_bg_color(r, C_BG,0); lv_obj_set_style_border_width(r,0,0); lv_obj_set_style_pad_all(r,0,0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    sb_state = L(r, "AI待机", C_SUB, F_CN); lv_obj_align(sb_state, LV_ALIGN_LEFT_MID, PAD, 0);
    sb_time = L(r, "00:00", C_TXT, F20); lv_obj_align(sb_time, LV_ALIGN_CENTER, 0, 0);
    sb_batt = L(r, "未连接", C_DIM, F_CN); lv_obj_align(sb_batt, LV_ALIGN_RIGHT_MID, -PAD, 0);
    sb_wifi = L(r, "", C_DIM, F_CN); lv_obj_align(sb_wifi, LV_ALIGN_RIGHT_MID, -80, 0);
    sb_radar = L(r, "麦离线", C_RED, F_CN); lv_obj_align(sb_radar, LV_ALIGN_RIGHT_MID, -140, 0);
}

/* ==================== 首页左卡 ==================== */
static void mk_home_left(lv_obj_t *p) {
    lv_coord_t cw=300, ch=240;
    lv_obj_t *c = C(p, cw, ch); lv_obj_set_pos(c, PAD, B_H+GAP);

    /* 圆形进度弧 */
    home_arc = lv_arc_create(c);
    lv_obj_set_size(home_arc, 140, 140);
    lv_obj_align(home_arc, LV_ALIGN_TOP_MID, 0, 12);
    lv_arc_set_range(home_arc, 0, 100);
    lv_arc_set_value(home_arc, 82);
    lv_arc_set_bg_angles(home_arc, 0, 360);
    lv_obj_set_style_arc_color(home_arc, C_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(home_arc, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(home_arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(home_arc, 10, LV_PART_MAIN);
    lv_obj_remove_style(home_arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(home_arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(home_arc, 0, 0);

    /* 分数文字在弧中心 */
    home_score_val = L(c, "--", C_TXT, F48);
    lv_obj_align_to(home_score_val, home_arc, LV_ALIGN_CENTER, 0, -8);
    L(c, "音频评分", C_SUB, F_CN);
    lv_obj_align_to(lv_obj_get_child(c, lv_obj_get_child_cnt(c)-1), home_arc, LV_ALIGN_CENTER, 0, 20);

    /* 风险 */
    home_risk_val = L(c, "呼吸风险: 低", C_GREEN, F_CN);
    lv_obj_align(home_risk_val, LV_ALIGN_BOTTOM_MID, 0, -56);

    /* 胶囊按钮 */
    home_capsule_btn = lv_btn_create(c);
    lv_obj_set_size(home_capsule_btn, 240, 42);
    lv_obj_align(home_capsule_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_radius(home_capsule_btn, 21, 0);
    lv_obj_set_style_bg_color(home_capsule_btn, C_CARD2, 0);
    lv_obj_set_style_border_width(home_capsule_btn, 1, 0);
    lv_obj_set_style_border_color(home_capsule_btn, C_BORDER, 0);
    lv_obj_set_style_shadow_width(home_capsule_btn, 0, 0);
    lv_obj_set_style_pad_all(home_capsule_btn, 0, 0);
    lv_obj_clear_flag(home_capsule_btn, LV_OBJ_FLAG_SCROLLABLE);
    home_capsule_lbl = L(home_capsule_btn, "点击开始睡眠监测", C_SUB, F_CN);
    lv_obj_center(home_capsule_lbl);
}

/* ==================== 首页右卡 OV-Watch 2x2 风格 ==================== */
static void mk_home_right(lv_obj_t *p) {
    lv_coord_t cx = PAD+300+GAP, cw = LCD_W-cx-PAD, ch=248;
    lv_obj_t *c = C(p, cw, ch); lv_obj_set_pos(c, cx, B_H+GAP);

    /* 标题 */
    home_dash_title = L(c, "AI 睡眠床头监护仪", C_TXT, F_CN);
    lv_obj_set_pos(home_dash_title, 16, 12);

    /* 动态副标题 */
    home_dash_detail = L(c, "等待数据接入...", C_SUB, F_CN);
    lv_obj_set_pos(home_dash_detail, 16, 36);
    lv_obj_set_width(home_dash_detail, cw-32);

    /* 2x2 指标卡片 */
    lv_coord_t mw = (cw-48)/2, mh = 94;
    lv_coord_t mx[2] = {16, 16+mw+16};
    lv_coord_t my[2] = {52, 52+mh+12};

    struct {lv_color_t bar; const char*n; const char*u; lv_obj_t **val;} cards[4]={
        {C_RED,    "心率",  "bpm",    &home_card_hr},
        {C_ORANGE, "呼声",  "dB",     &home_card_sn},
        {C_CYAN,   "呼吸",  "/min",   &home_card_br},
        {C_GREEN,  "血氧",  "%",      &home_card_stage},
    };

    for(int i=0;i<4;i++){
        lv_obj_t*mc=C(c,mw,mh);lv_obj_set_pos(mc,mx[i%2],my[i/2]);
        lv_obj_set_style_radius(mc,12,0);lv_obj_set_style_bg_color(mc,C_CARD2,0);

        /* 顶部色条 */
        lv_obj_t*bar=lv_obj_create(mc);lv_obj_set_size(bar,mw,4);lv_obj_set_pos(bar,0,0);
        lv_obj_set_style_radius(bar,0,0);lv_obj_set_style_bg_color(bar,cards[i].bar,0);
        lv_obj_set_style_border_width(bar,0,0);

        L(mc,cards[i].n,C_SUB,F_CN);lv_obj_set_pos(lv_obj_get_child(mc,lv_obj_get_child_cnt(mc)-1),12,12);
        *cards[i].val=L(mc,"--",C_TXT,F48);lv_obj_set_pos(*cards[i].val,12,32);
        if(cards[i].u[0]){L(mc,cards[i].u,C_SUB,F_CN);lv_obj_set_pos(lv_obj_get_child(mc,lv_obj_get_child_cnt(mc)-1),90,54);}
    }
}

/* ==================== 首页六宫格 ==================== */
static void mk_home_grid(lv_obj_t *p) {
    const char*ns[]={"实时监护","打呼波形","睡眠报告","呼吸事件","AI建议","设置连接"};
    const char*is[]={"HR","SN","RP","EV","AI","ST"};
    const char*ss[]={"生命体征","声音趋势","睡眠总结","异常事件","生活建议","设备管理"};
    lv_color_t cs[]={C_BLUE,C_PURPLE,C_GREEN,C_ORANGE,C_CYAN,C_SUB};

    lv_coord_t bw=(LCD_W-2*PAD-2*GAP)/3, bh=70;
    lv_coord_t y0=B_H+GAP+240+GAP+4, y1=y0+bh+GAP;

    for(int i=0;i<6;i++){
        lv_coord_t cx=PAD+(i%3)*(bw+GAP), cy=(i/3)?y1:y0;
        entry_btn[i]=B(p,bw,bh,C_CARD); lv_obj_set_pos(entry_btn[i],cx,cy);
        lv_obj_set_style_border_width(entry_btn[i],1,0); lv_obj_set_style_border_color(entry_btn[i],C_BORDER,0);

        lv_obj_t *o=lv_obj_create(entry_btn[i]); lv_obj_set_size(o,38,38); lv_obj_set_pos(o,14,16);
        lv_obj_set_style_radius(o,19,0); lv_obj_set_style_bg_color(o,cs[i],0); lv_obj_set_style_border_width(o,0,0);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *ic=L(o,is[i],C_TXT,F20); lv_obj_center(ic);

        entry_lbl[i]=L(entry_btn[i],ns[i],C_TXT,F_CN); lv_obj_set_pos(entry_lbl[i],64,14);
        L(entry_btn[i],ss[i],C_SUB,F_CN); lv_obj_set_pos(lv_obj_get_child(entry_btn[i],lv_obj_get_child_cnt(entry_btn[i])-1),64,40);
    }
}

/* 手势检测 (手动追踪坐标, LV_OBJ_FLAG_GESTURE 在此版本不可用) */
static lv_coord_t swipe_start_x = 0;
static bool swipe_tracking = false;

static void home_swipe_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t pt; lv_indev_get_point(indev, &pt);
    if (code == LV_EVENT_PRESSED) { swipe_start_x = pt.x; swipe_tracking = true; }
    else if (code == LV_EVENT_RELEASED && swipe_tracking) {
        if (swipe_start_x - pt.x > 80) sleep_assistant_ui_show(); /* 左滑 */
        swipe_tracking = false;
    }
}

static void mk_home(void) {
    pages[PG_HOME]=lv_obj_create(scr); lv_obj_set_size(pages[PG_HOME],LCD_W,LCD_H); lv_obj_set_pos(pages[PG_HOME],0,0);
    lv_obj_set_style_bg_color(pages[PG_HOME],C_BG,0); lv_obj_set_style_border_width(pages[PG_HOME],0,0);
    lv_obj_set_style_pad_all(pages[PG_HOME],0,0); lv_obj_clear_flag(pages[PG_HOME],LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pages[PG_HOME], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pages[PG_HOME], home_swipe_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(pages[PG_HOME], home_swipe_cb, LV_EVENT_RELEASED, NULL);
    mk_bar(pages[PG_HOME]); mk_home_left(pages[PG_HOME]); mk_home_right(pages[PG_HOME]); mk_home_grid(pages[PG_HOME]);
}

/* ==================== 子页面框架 ==================== */
static lv_obj_t *mk_sub(int id, const char *ttl, bool scroll) {
    lv_obj_t *pg=lv_obj_create(scr); lv_obj_set_size(pg,LCD_W,LCD_H); lv_obj_set_pos(pg,0,0);
    lv_obj_set_style_bg_color(pg,C_BG,0); lv_obj_set_style_border_width(pg,0,0); lv_obj_set_style_pad_all(pg,0,0);
    lv_obj_add_flag(pg,LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(pg,LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top=lv_obj_create(pg); lv_obj_set_size(top,LCD_W,B_H+4); lv_obj_set_pos(top,0,0);
    lv_obj_set_style_bg_color(top,C_CARD,0); lv_obj_set_style_border_width(top,0,0); lv_obj_set_style_pad_all(top,0,0);
    lv_obj_clear_flag(top,LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bk=lv_btn_create(top); lv_obj_set_size(bk,56,28); lv_obj_set_pos(bk,8,6);
    lv_obj_set_style_radius(bk,14,0); lv_obj_set_style_bg_color(bk,C_CARD2,0); lv_obj_set_style_shadow_width(bk,0,0);
    lv_obj_set_style_pad_all(bk,0,0); L(bk,"返回",C_TXT,F_CN); lv_obj_center(lv_obj_get_child(bk,0));

    L(top,ttl,C_TXT,F_CN); lv_obj_align(lv_obj_get_child(top,lv_obj_get_child_cnt(top)-1),LV_ALIGN_CENTER,0,0);

    lv_obj_t *bd=lv_obj_create(pg); lv_obj_set_size(bd,LCD_W,LCD_H-B_H-4); lv_obj_set_pos(bd,0,B_H+4);
    lv_obj_set_style_bg_opa(bd,LV_OPA_TRANSP,0); lv_obj_set_style_border_width(bd,0,0); lv_obj_set_style_pad_all(bd,PAD,0);
    lv_obj_set_flex_flow(bd,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bd,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(bd,GAP,0);
    if(scroll){lv_obj_set_scrollbar_mode(bd,LV_SCROLLBAR_MODE_OFF); lv_obj_set_scroll_dir(bd,LV_DIR_VER);}
    else lv_obj_clear_flag(bd,LV_OBJ_FLAG_SCROLLABLE);

    pages[id]=pg; return bd;
}

/* ==================== 子页: 实时监护 ==================== */
static void mk_monitor(void) {
    lv_obj_t *bd=mk_sub(PG_MONITOR,"实时监护",false);
    struct{const char*n;const char*u;}it[]={{"SpO2","%"},{"心率","bpm"},{"呼吸","/min"},{"鼾声","dB"}};
    lv_obj_t *r1=lv_obj_create(bd); lv_obj_set_size(r1,LV_PCT(100),120);
    lv_obj_set_style_bg_opa(r1,LV_OPA_TRANSP,0); lv_obj_set_style_border_width(r1,0,0); lv_obj_set_style_pad_all(r1,0,0);
    lv_obj_set_flex_flow(r1,LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r1,LV_FLEX_ALIGN_SPACE_EVENLY,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(r1,LV_OBJ_FLAG_SCROLLABLE);
    lv_coord_t cw=(LCD_W-2*PAD-3*GAP)/4;
    for(int i=0;i<4;i++){lv_obj_t*cd=C(r1,cw,120);L(cd,it[i].n,C_SUB,F_CN);lv_obj_set_pos(lv_obj_get_child(cd,0),12,10);monitor_metric_val[i]=L(cd,"--",C_TXT,F48);lv_obj_set_pos(monitor_metric_val[i],12,40);L(cd,it[i].u,C_SUB,F_CN);lv_obj_set_pos(lv_obj_get_child(cd,lv_obj_get_child_cnt(cd)-1),12,96);}
    lv_obj_t*r2=C(bd,LV_PCT(100),170);
    /* 增加到 7 行: 加声学指标 */
    const char*st[]={"体位: --","当前事件: --","AHI: -- /h","鼾声分类: --","声音参数: --"};
    for(int i=0;i<5;i++){monitor_info_val[i]=L(r2,st[i],C_TXT,F_CN);lv_obj_set_pos(monitor_info_val[i],16,12+i*28);}
    /* 声学指标行用Alimama(无鼾字,字全) */
}

/* ==================== 子页: 打呼波形 ==================== */
static void mk_wave(void) {
    lv_obj_t *bd=mk_sub(PG_WAVE,"打呼与呼吸波形",true);
    mk_chart_card(bd,"打呼强度 dB",C_PURPLE,0,100,&wave_chart[0],&wave_ser[0]);
    mk_chart_card(bd,"呼吸波形",C_CYAN,0,255,&wave_chart[1],&wave_ser[1]);
    mk_chart_card(bd,"血氧趋势 %",C_ORANGE,80,100,&wave_chart[2],&wave_ser[2]);
    lv_obj_t*st=C(bd,LV_PCT(100),44);wave_status_lbl=L(st,"等待实时数据",C_SUB,F_CN);lv_obj_center(wave_status_lbl);
}

/* ==================== 子页: 睡眠报告 (可滚动) ==================== */
static void mk_report(void) {
    lv_obj_t *bd=mk_sub(PG_REPORT,"睡眠报告",true);
    lv_obj_t*c1=C(bd,LV_PCT(100),80);
    L(c1,"睡眠评分",C_SUB,F_CN);lv_obj_set_pos(lv_obj_get_child(c1,0),16,10);
    report_score_lbl=L(c1,"--",C_TXT,F48);lv_obj_set_pos(report_score_lbl,140,8);
    report_risk_lbl=L(c1,"呼吸风险: --",C_GREEN,F_CN);lv_obj_set_pos(report_risk_lbl,16,58);
    lv_obj_t*c2=C(bd,LV_PCT(100),170);
    const char*ns[]={"总睡眠","AHI","最低血氧","T90","心率变化","呼吸事件","鼾声次数","仰卧事件","最大鼾声"};
    const char*us[]={"--","-- /h","--%","--%","-- bpm","-- 次","-- 次","--%","-- dB"};
    for(int i=0;i<9;i++){char bf[64];snprintf(bf,sizeof(bf),"%s  %s",ns[i],us[i]);report_metric_lbl[i]=L(c2,bf,C_TXT,F_CN);lv_obj_set_pos(report_metric_lbl[i],16+(i%2)*220,12+(i/2)*26);}
    lv_obj_t*c3=C(bd,LV_PCT(100),140);
    L(c3,"综合评价",C_SUB,F_CN);lv_obj_set_pos(lv_obj_get_child(c3,0),16,10);
    report_eval_lbl=L(c3,"等待足够数据后生成声息分类与风险建议。",C_TXT,F_EXT);
    lv_obj_set_pos(report_eval_lbl,16,34);
    lv_obj_set_width(report_eval_lbl,LV_PCT(100)-32);
    lv_label_set_long_mode(report_eval_lbl,LV_LABEL_LONG_WRAP);
}

/* ==================== 子页: 呼吸事件 ==================== */
static void mk_event(void) {
    lv_obj_t *bd=mk_sub(PG_EVENT,"呼吸事件",true);
    lv_obj_t*c1=C(bd,LV_PCT(100),100);
    event_summary_lbl[0]=L(c1,"呼吸异常事件: 0 次",C_TXT,F_CN);lv_obj_set_pos(event_summary_lbl[0],16,12);
    event_summary_lbl[1]=L(c1,"AHI: -- /h",C_TXT,F_CN);lv_obj_set_pos(event_summary_lbl[1],16,36);
    event_summary_lbl[2]=L(c1,"风险等级: --",C_TXT,F_CN);lv_obj_set_pos(event_summary_lbl[2],16,60);
    L(c1,"本设备仅提供风险提示，不作为医学诊断。",C_DIM,F_CN);lv_obj_set_pos(lv_obj_get_child(c1,lv_obj_get_child_cnt(c1)-1),16,80);
    lv_obj_t*c2=C(bd,LV_PCT(100),200);
    L(c2,"事件记录",C_SUB,F_CN);lv_obj_set_pos(lv_obj_get_child(c2,0),16,10);
    event_record_lbl=L(c2,"暂无异常事件记录\n\n事件将显示持续时间、气流下降、\n血氧下降和心率变化。",C_DIM,F_CN);
    lv_obj_set_pos(event_record_lbl,16,34);
    lv_obj_set_width(event_record_lbl,LV_PCT(100)-32);
    lv_label_set_long_mode(event_record_lbl,LV_LABEL_LONG_WRAP);
}

/* ==================== 子页: AI建议 ==================== */
static lv_obj_t *ai_text;

static void ai_refresh(SleepData_t*d){
    if(!ai_text||!lv_obj_is_valid(ai_text)) return;
    char b[512];
    int sn = d->snore_db;
    int ap = d->apnea_count;
    int hp = d->hypopnea_count;
    bool no_spo2 = (d->spo2<=0);
    bool no_main = !d->main_online;

    int len=0;
    if(no_main){
        len=snprintf(b,sizeof(b),"主机未接入\n");
        if(d->watch_spo2_valid)
            len+=snprintf(b+len,sizeof(b)-len,"手表血氧 %d%%\n",d->watch_spo2);
        else
            len+=snprintf(b+len,sizeof(b)-len,"等待血氧数据\n");
        if(d->snore_count>0)
            len+=snprintf(b+len,sizeof(b)-len,"已记录鼾声%d次\n\n",d->snore_count);
        else
            len+=snprintf(b+len,sizeof(b)-len,"等待鼾声事件\n\n");
    } else {
        /* 只用sleep字体确认有的字 */
        if(ap+hp>0)
            len=snprintf(b,sizeof(b),"暂停%d次 低通%d次\n关注睡姿和呼吸\n\n",ap,hp);
        else
            len=snprintf(b,sizeof(b),"呼吸平稳 无异常\n\n");
        if(sn>50)
            len+=snprintf(b+len,sizeof(b)-len,"呼声%d dB 较响\n建议侧卧 鼻通\n\n",sn);
        else if(sn>0)
            len+=snprintf(b+len,sizeof(b)-len,"呼声%d dB\n\n",sn);
        if(no_spo2)
            len+=snprintf(b+len,sizeof(b)-len,"无血氧数据\n呼吸风险仅参考\n\n");
    }
    len+=snprintf(b+len,sizeof(b)-len,"---\n保持规律作息\n睡前勿饮咖啡酒精\n长期憋醒或困倦\n建议就医");
    lv_label_set_text(ai_text,b);
}

static void mk_ai(void) {
    lv_obj_t *bd=mk_sub(PG_AI,"AI 建议",true);
    lv_obj_t*c1=C(bd,LV_PCT(100),340);
    L(c1,"今日建议",C_SUB,F_CN);lv_obj_set_pos(lv_obj_get_child(c1,0),16,8);
    ai_text=L(c1,"主机已连接",C_GREEN,F_CN);
    lv_obj_set_pos(ai_text,16,34);
    lv_obj_set_width(ai_text,LV_PCT(100)-32);
    lv_label_set_long_mode(ai_text,LV_LABEL_LONG_WRAP);
}

/* ==================== 设置页工具 ==================== */
static lv_obj_t *SR(lv_obj_t*p,const char*n,const char*v,lv_color_t vc){
    lv_obj_t*r=lv_obj_create(p);lv_obj_set_size(r,LV_PCT(100),42);lv_obj_set_style_bg_opa(r,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(r,0,0);lv_obj_set_style_pad_all(r,0,0);lv_obj_clear_flag(r,LV_OBJ_FLAG_SCROLLABLE);
    L(r,n,C_TXT,F_CN);lv_obj_align(lv_obj_get_child(r,0),LV_ALIGN_LEFT_MID,12,0);
    if(v){L(r,v,vc,F_CN);lv_obj_align(lv_obj_get_child(r,lv_obj_get_child_cnt(r)-1),LV_ALIGN_RIGHT_MID,-12,0);}
    return r;
}
static lv_obj_t *SB(lv_obj_t*p,const char*t,lv_color_t c,lv_event_cb_t cb){
    lv_obj_t*b=lv_btn_create(p);lv_obj_set_size(b,74,30);lv_obj_set_style_radius(b,8,0);lv_obj_set_style_bg_color(b,c,0);
    lv_obj_set_style_bg_color(b,lv_color_darken(c,LV_OPA_20),LV_STATE_PRESSED);lv_obj_set_style_border_width(b,0,0);
    lv_obj_set_style_shadow_width(b,0,0);lv_obj_set_style_pad_all(b,0,0);lv_obj_clear_flag(b,LV_OBJ_FLAG_SCROLLABLE);
    L(b,t,C_TXT,F_CN);lv_obj_center(lv_obj_get_child(b,0));if(cb)SCB(b,cb,LV_EVENT_CLICKED);return b;
}
static lv_obj_t *SC(lv_obj_t*p,const char*ttl,int rs){
    lv_coord_t h=46+rs*42+8;lv_obj_t*c=C(p,LCD_W-2*PAD,h);
    L(c,ttl,C_SUB,F_CN);lv_obj_set_pos(lv_obj_get_child(c,0),14,10);
    lv_obj_t*sp=lv_obj_create(c);lv_obj_set_size(sp,LCD_W-2*PAD-28,1);lv_obj_set_pos(sp,14,34);
    lv_obj_set_style_bg_color(sp,C_BORDER,0);lv_obj_set_style_border_width(sp,0,0);return c;
}
static lv_obj_t *SA(lv_obj_t*p,const char*n,const char*bt,lv_color_t bc,lv_event_cb_t cb){
    lv_obj_t*r=SR(p,n,NULL,C_TXT);SB(r,bt,bc,cb);lv_obj_align(lv_obj_get_child(r,lv_obj_get_child_cnt(r)-1),LV_ALIGN_RIGHT_MID,-12,0);return r;
}

/* ==================== 子页: 设置连接 (可滚动) ==================== */
static void mk_setting(void) {
    lv_obj_t *bd=mk_sub(PG_SETTING,"设置连接",true);
    lv_obj_t*c1=SC(bd,"连接设置",3);SA(c1,"WiFi 配网","配网",C_BLUE,NULL);SA(c1,"断开 WiFi","断开",C_WARN,NULL);SA(c1,"同步时间","同步",C_GREEN,NULL);
    lv_obj_t*c2=SC(bd,"传感器",3);SA(c2,"雷达重启","重启",C_WARN,NULL);SA(c2,"雷达校准","校准",C_BLUE,NULL);SA(c2,"麦克风测试","测试",C_GREEN,NULL);
    lv_obj_t*c3=SC(bd,"数据管理",3);SR(c3,"SD 卡状态","未接入",C_DIM);SA(c3,"导出数据","导出",C_BLUE,NULL);SA(c3,"清空历史","清空",C_RED,NULL);
    lv_obj_t*c4=SC(bd,"设备",4);SR(c4,"屏幕亮度","可调",C_SUB);SR(c4,"系统音量","可调",C_SUB);SA(c4,"恢复默认","重置",C_RED,NULL);SR(c4,"固件版本","v24.07",C_SUB);
    setting_body=bd;
}

/* ==================== 操作菜单 ==================== */
static void act_menu_mk(void){
    if(act_menu) return;
    act_menu=lv_obj_create(scr);
    lv_obj_set_size(act_menu,LCD_W,LCD_H);lv_obj_set_pos(act_menu,0,0);
    lv_obj_set_style_bg_color(act_menu,lv_color_hex(0x000000),0);lv_obj_set_style_bg_opa(act_menu,LV_OPA_50,0);
    lv_obj_set_style_border_width(act_menu,0,0);lv_obj_set_style_pad_all(act_menu,0,0);
    lv_obj_add_flag(act_menu,LV_OBJ_FLAG_HIDDEN);lv_obj_clear_flag(act_menu,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(act_menu,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t*bx=C(act_menu,320,240);lv_obj_center(bx);lv_obj_set_style_pad_all(bx,16,0);
    L(bx,"睡眠监测管理",C_TXT,F_CN);lv_obj_align(lv_obj_get_child(bx,0),LV_ALIGN_TOP_MID,0,12);
    struct{const char*t;lv_color_t c;}it[]={{"开始监测",C_GREEN},{"暂停监测",C_WARN},{"结束本次睡眠",C_RED},{"进入免打扰",C_PURPLE}};
    for(int i=0;i<4;i++){lv_obj_t*btn=lv_btn_create(bx);lv_obj_set_size(btn,280,42);lv_obj_set_pos(btn,12,48+i*46);lv_obj_set_style_radius(btn,10,0);lv_obj_set_style_bg_color(btn,C_CARD2,0);lv_obj_set_style_border_width(btn,1,0);lv_obj_set_style_border_color(btn,C_BORDER,0);lv_obj_set_style_shadow_width(btn,0,0);lv_obj_set_style_pad_all(btn,0,0);lv_obj_clear_flag(btn,LV_OBJ_FLAG_SCROLLABLE);L(btn,it[i].t,it[i].c,F_CN);lv_obj_center(lv_obj_get_child(btn,0));}
}
static void act_show(void){if(!act_menu)act_menu_mk();if(act_menu){lv_obj_clear_flag(act_menu,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(act_menu);}}
static void act_hide(void){if(act_menu)lv_obj_add_flag(act_menu,LV_OBJ_FLAG_HIDDEN);}

/* ==================== 确认弹窗 ==================== */
static void cfm_mk(void){
    if(cfm_dlg) return;
    cfm_dlg=lv_obj_create(scr);
    lv_obj_set_size(cfm_dlg,LCD_W,LCD_H);lv_obj_set_pos(cfm_dlg,0,0);
    lv_obj_set_style_bg_color(cfm_dlg,lv_color_hex(0x000000),0);lv_obj_set_style_bg_opa(cfm_dlg,LV_OPA_60,0);
    lv_obj_set_style_border_width(cfm_dlg,0,0);lv_obj_set_style_pad_all(cfm_dlg,0,0);
    lv_obj_add_flag(cfm_dlg,LV_OBJ_FLAG_HIDDEN);lv_obj_clear_flag(cfm_dlg,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(cfm_dlg,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t*bx=C(cfm_dlg,380,190);lv_obj_center(bx);lv_obj_set_style_pad_all(bx,20,0);
    cfm_txt=L(bx,"确定要执行此操作吗?",C_TXT,F_CN);lv_obj_align(cfm_txt,LV_ALIGN_TOP_MID,0,20);lv_obj_set_width(cfm_txt,340);lv_label_set_long_mode(cfm_txt,LV_LABEL_LONG_WRAP);lv_obj_set_style_text_align(cfm_txt,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_t*cb=B(bx,130,46,C_CARD);lv_obj_set_style_border_width(cb,1,0);lv_obj_set_style_border_color(cb,C_BORDER,0);lv_obj_set_pos(cb,38,124);L(cb,"取消",C_SUB,F_CN);lv_obj_center(lv_obj_get_child(cb,0));
    lv_obj_t*ob=B(bx,130,46,C_RED);lv_obj_set_pos(ob,212,124);L(ob,"确定",C_TXT,F_CN);lv_obj_center(lv_obj_get_child(ob,0));
}
static void cfm_show(const char*m,void(*cb)(void)){if(!cfm_dlg)cfm_mk();if(!cfm_dlg)return;cfm_fn=cb;lv_label_set_text(cfm_txt,m);lv_obj_clear_flag(cfm_dlg,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(cfm_dlg);}
static void cfm_hide(void){if(cfm_dlg)lv_obj_add_flag(cfm_dlg,LV_OBJ_FLAG_HIDDEN);cfm_fn=NULL;}

/* ==================== 事件 ==================== */
static void ev_capsule(lv_event_t*e){(void)e;act_show();}
static void ev_audio_btn(lv_event_t*e){(void)e;sleep_player_ui_show();}
static void ev_abg(lv_event_t*e){if(lv_event_get_code(e)==LV_EVENT_CLICKED)act_hide();}
static void ev_as(lv_event_t*e){(void)e;mon=true;g_sleep_data.system_state=SYS_STATE_MONITORING;hardware_control_send(HW_CMD_MONITOR_START,0);act_hide();sleep_ui_refresh();}
static void ev_ap(lv_event_t*e){(void)e;hardware_control_send(HW_CMD_MONITOR_STOP,0);act_hide();}
static void ev_ae(lv_event_t*e){(void)e;mon=false;g_sleep_data.system_state=SYS_STATE_REPORT_READY;hardware_control_send(HW_CMD_MONITOR_STOP,0);act_hide();sleep_ui_refresh();}
static void ev_ad(lv_event_t*e){(void)e;dnd=!dnd;hardware_control_send(dnd?HW_CMD_DND_ON:HW_CMD_DND_OFF,0);act_hide();sleep_ui_refresh();}
static void ev_mon(lv_event_t*e){(void)e;sleep_ui_show_monitor();}
static void ev_wav(lv_event_t*e){(void)e;sleep_ui_show_wave();}
static void ev_rep(lv_event_t*e){(void)e;sleep_ui_show_report();}
static void ev_evt(lv_event_t*e){(void)e;sleep_ui_show_event();}
static void ev_ai(lv_event_t*e){(void)e;sleep_ui_show_ai();}
static void ev_set(lv_event_t*e){(void)e;sleep_ui_show_setting();}
static void ev_back(lv_event_t*e){(void)e;sleep_ui_show_home();}
static void ev_cc(lv_event_t*e){(void)e;cfm_hide();}
static void ev_co(lv_event_t*e){(void)e;if(cfm_fn)cfm_fn();cfm_hide();}
static void do_clr(void){hardware_control_send(HW_CMD_CLEAR_HISTORY,0);}
static void do_rst(void){hardware_control_send(HW_CMD_FACTORY_RESET,0);}
static void do_rr(void){hardware_control_send(HW_CMD_RADAR_RESET,0);}
static void ev_clr(lv_event_t*e){(void)e;cfm_show("确定要清空所有历史数据吗?\n此操作不可恢复。",do_clr);}
static void ev_rst(lv_event_t*e){(void)e;cfm_show("确定要恢复出厂设置吗?\n所有个人数据将被清除。",do_rst);}
static void ev_rr(lv_event_t*e){(void)e;cfm_show("确定要重启雷达模组吗?\n重启约需15秒恢复。",do_rr);}
static void ev_rc(lv_event_t*e){(void)e;hardware_control_send(HW_CMD_RADAR_CALIBRATE,0);}
static void ev_mt(lv_event_t*e){(void)e;hardware_control_send(HW_CMD_MIC_TEST,0);}
static void ev_wc(lv_event_t*e){(void)e;hardware_control_send(HW_CMD_WIFI_ON,0);}
static void ev_wd(lv_event_t*e){(void)e;hardware_control_send(HW_CMD_WIFI_OFF,0);}
static void ev_st(lv_event_t*e){(void)e;hardware_control_send(HW_CMD_SYNC_TIME,0);}
static void ev_ex(lv_event_t*e){(void)e;hardware_control_send(HW_CMD_EXPORT_DATA,0);}

/* ==================== 设置页事件绑定 ==================== */
static void setting_hook(void){
    if(!setting_body){ESP_LOGE("ui","setting_body NULL");return;}
    lv_obj_t*c1=lv_obj_get_child(setting_body,0);if(!c1){ESP_LOGE("ui","c1 NULL");return;}
    lv_event_cb_t cb1[]={ev_wc,ev_wd,ev_st};
    for(int i=0;i<3;i++){lv_obj_t*r=lv_obj_get_child(c1,2+i);if(r){lv_obj_t*b=lv_obj_get_child(r,lv_obj_get_child_cnt(r)-1);SCB(b,cb1[i],LV_EVENT_CLICKED);}}
    lv_obj_t*c2=lv_obj_get_child(setting_body,1);if(!c2){ESP_LOGE("ui","c2 NULL");return;}
    lv_event_cb_t cb2[]={ev_rr,ev_rc,ev_mt};
    for(int i=0;i<3;i++){lv_obj_t*r=lv_obj_get_child(c2,2+i);if(r){lv_obj_t*b=lv_obj_get_child(r,lv_obj_get_child_cnt(r)-1);SCB(b,cb2[i],LV_EVENT_CLICKED);}}
    lv_obj_t*c3=lv_obj_get_child(setting_body,2);if(!c3){ESP_LOGE("ui","c3 NULL");return;}
    lv_obj_t*re=lv_obj_get_child(c3,3);if(re){lv_obj_t*b=lv_obj_get_child(re,lv_obj_get_child_cnt(re)-1);SCB(b,ev_ex,LV_EVENT_CLICKED);}
    lv_obj_t*rc=lv_obj_get_child(c3,4);if(rc){lv_obj_t*b=lv_obj_get_child(rc,lv_obj_get_child_cnt(rc)-1);SCB(b,ev_clr,LV_EVENT_CLICKED);}
    lv_obj_t*c4=lv_obj_get_child(setting_body,3);if(!c4){ESP_LOGE("ui","c4 NULL");return;}
    lv_obj_t*rf=lv_obj_get_child(c4,4);if(rf){lv_obj_t*b=lv_obj_get_child(rf,lv_obj_get_child_cnt(rf)-1);SCB(b,ev_rst,LV_EVENT_CLICKED);}
}

/* ==================== 页面切换 ==================== */
static void hide_all(void){for(int i=0;i<PG_CNT;i++)if(pages[i])lv_obj_add_flag(pages[i],LV_OBJ_FLAG_HIDDEN);if(cfm_dlg)lv_obj_add_flag(cfm_dlg,LV_OBJ_FLAG_HIDDEN);if(act_menu)lv_obj_add_flag(act_menu,LV_OBJ_FLAG_HIDDEN);}
static void show(int id){hide_all();if(pages[id])lv_obj_clear_flag(pages[id],LV_OBJ_FLAG_HIDDEN);cur_page=id;}
void sleep_ui_show_home(void){show(PG_HOME);}void sleep_ui_show_monitor(void){show(PG_MONITOR);}
void sleep_ui_show_wave(void){show(PG_WAVE);}void sleep_ui_show_report(void){show(PG_REPORT);}
void sleep_ui_show_event(void){show(PG_EVENT);}void sleep_ui_show_ai(void){show(PG_AI);}
void sleep_ui_show_setting(void){show(PG_SETTING);}void sleep_ui_show_assistant(void){sleep_assistant_ui_show();}void sleep_ui_show_player(void){sleep_player_ui_show();}int sleep_ui_get_current_page(void){return cur_page;}

/* ==================== 状态设置器 ==================== */
void sleep_ui_set_monitoring(bool on){mon=on;}void sleep_ui_set_dnd(bool on){dnd=on;}
void sleep_ui_set_night_mode(bool on){}void sleep_ui_set_keep_screen_on(bool on){keep=on;}
void sleep_ui_set_mic_enabled(bool on){mic=on;}void sleep_ui_set_radar_enabled(bool on){radar=on;}
void sleep_ui_set_mute(bool on){mute=on;}
void sleep_ui_update_time(int h,int m,int s){snprintf(time_str,sizeof(time_str),"%02d:%02d:%02d",h,m,s);if(sb_time&&lv_obj_is_valid(sb_time))lv_label_set_text(sb_time,time_str);}

static void set_label(lv_obj_t *o,const char *txt){if(o&&lv_obj_is_valid(o))lv_label_set_text(o,txt);}
static void fmt_min(char *b,size_t l,uint32_t m){snprintf(b,l,"%luh%02lu",(unsigned long)(m/60),(unsigned long)(m%60));}
static void refresh_monitor_page(SleepData_t*d,bool radar_online,bool mic_online,bool env_stale){
    bool main_ok = d->main_online;
    char b[96];
    if(monitor_metric_val[0]&&lv_obj_is_valid(monitor_metric_val[0])){if(d->spo2>0)lv_label_set_text_fmt(monitor_metric_val[0],"%d",d->spo2);else lv_label_set_text(monitor_metric_val[0],"--");}
    if(monitor_metric_val[1]&&lv_obj_is_valid(monitor_metric_val[1])){if(d->heart_rate>0)lv_label_set_text_fmt(monitor_metric_val[1],"%d",d->heart_rate);else lv_label_set_text(monitor_metric_val[1],"--");}
    if(monitor_metric_val[2]&&lv_obj_is_valid(monitor_metric_val[2])){if((main_ok||radar_online)&&d->breath_rate>0)lv_label_set_text_fmt(monitor_metric_val[2],"%d",d->breath_rate);else lv_label_set_text(monitor_metric_val[2],"--");}
    if(monitor_metric_val[3]&&lv_obj_is_valid(monitor_metric_val[3])){if(mic_online)lv_label_set_text_fmt(monitor_metric_val[3],"%d",d->snore_db);else lv_label_set_text(monitor_metric_val[3],"--");}
    snprintf(b,sizeof(b),"在床: %s",d->in_bed?"Y":"N");set_label(monitor_info_val[0],b);
    snprintf(b,sizeof(b),"当前事件: %s",main_ok?main_event_label((uint8_t)d->event_id):"--");set_label(monitor_info_val[1],b);
    if(main_ok)snprintf(b,sizeof(b),"AHI: %.1f /h  事件置信%d",d->ahi,d->event_confidence);else snprintf(b,sizeof(b),"AHI: --");set_label(monitor_info_val[2],b);
    /* 鼾声分类 (ST_s标签: 鼻鼾/喉鼾/口呼吸鼾/混合型鼾/无鼾声, sleep字体有) */
    {
        const char *snore_type_str  = mic_online ? ST_s(d->snore_type) : "--";
        lv_color_t snore_type_color = mic_online ? ST_c(d->snore_type) : C_DIM;
        if (mic_online && d->snore_type_confidence > 0)
            snprintf(b,sizeof(b),"鼾声分类: %s (%d%%)", snore_type_str, d->snore_type_confidence);
        else
            snprintf(b,sizeof(b),"鼾声分类: %s", snore_type_str);
        set_label(monitor_info_val[3], b);
        if (monitor_info_val[3] && lv_obj_is_valid(monitor_info_val[3]))
            lv_obj_set_style_text_color(monitor_info_val[3], snore_type_color, 0);
    }
    /* 声学指标 */
    {
        if (mic_online && d->spectral_centroid_hz > 0)
            snprintf(b,sizeof(b),"声音: %dHz 低%d H%d",
                     d->spectral_centroid_hz, d->low_freq_ratio_x100, d->harmonic_ratio_x100);
        else
            snprintf(b,sizeof(b),"声学: --");
        set_label(monitor_info_val[4], b);
    }
    (void)env_stale;
}
static void chart_from_ring(lv_obj_t *chart,lv_chart_series_t *ser,const uint8_t *buf,int size,uint8_t idx,uint8_t cnt){
    if(!chart||!ser||!lv_obj_is_valid(chart))return;
    for(int i=0;i<WAVE_POINTS;i++)lv_chart_set_value_by_id(chart,ser,i,LV_CHART_POINT_NONE);
    int n=cnt;if(n>WAVE_POINTS)n=WAVE_POINTS;if(n>size)n=size;
    int start=(idx+size-n)%size;
    for(int i=0;i<n;i++)lv_chart_set_value_by_id(chart,ser,WAVE_POINTS-n+i,buf[(start+i)%size]);
    lv_chart_refresh(chart);
}
static void refresh_wave_page(SleepData_t*d,bool radar_online,bool mic_online){
    bool breath_wave = false;
    if(wave_chart[0]&&wave_ser[0]&&lv_obj_is_valid(wave_chart[0])){
        lv_chart_set_next_value(wave_chart[0],wave_ser[0],mic_online?CL(d->snore_db,0,100):0);lv_chart_refresh(wave_chart[0]);
    }
#if CONFIG_ENABLE_R60_RADAR
    {sleep_radar_data_t r;bool rs=sleep_radar_data_get_snapshot(&r);
    breath_wave=rs&&fresh_ms(r.last_breath_wave_ms,3000)&&r.breath_wave_count>0;
    if(breath_wave)chart_from_ring(wave_chart[1],wave_ser[1],r.breath_wave_buf,BREATH_WAVE_BUF_SIZE,r.breath_wave_idx,r.breath_wave_count);
    else}
#endif
    if(wave_chart[1]&&wave_ser[1]&&lv_obj_is_valid(wave_chart[1])){
        int v=(radar_online&&d->breath_rate>0)?CL(d->breath_rate*8,0,255):0;
        lv_chart_set_next_value(wave_chart[1],wave_ser[1],v);lv_chart_refresh(wave_chart[1]);
    }
    if(wave_chart[2]&&wave_ser[2]&&lv_obj_is_valid(wave_chart[2])){
        lv_chart_set_next_value(wave_chart[2],wave_ser[2],d->spo2>0?CL(d->spo2,80,100):80);lv_chart_refresh(wave_chart[2]);
    }
    if(wave_status_lbl&&lv_obj_is_valid(wave_status_lbl)){
        char b[96];snprintf(b,sizeof(b),"麦克风:%s  呼吸:%s  血氧:%d%%%s",mic_online?"在线":"离线",breath_wave?"原始波形":"趋势",d->spo2,d->spo2_from_watch?"手表":"");
        lv_label_set_text(wave_status_lbl,b);lv_obj_set_style_text_color(wave_status_lbl,(mic_online||radar_online)?C_SUB:C_WARN,0);
    }
}
static void refresh_report_page(SleepData_t*d,bool radar_online,bool mic_online){
#if CONFIG_ENABLE_R60_RADAR
    sleep_radar_data_t r;bool rs=sleep_radar_data_get_snapshot(&r);
    uint32_t sleep_total=0;if(rs)sleep_total=r.light_sleep_min+r.deep_sleep_min;
#else
    bool rs=false;
    uint32_t sleep_total=0;
#endif
    if(report_score_lbl&&lv_obj_is_valid(report_score_lbl)){if(mic_online||radar_online)lv_label_set_text_fmt(report_score_lbl,"%d",d->sleep_score);else lv_label_set_text(report_score_lbl,"--");}
    if(report_risk_lbl&&lv_obj_is_valid(report_risk_lbl)){lv_label_set_text_fmt(report_risk_lbl,"呼吸风险: %s",D_risk(d));lv_obj_set_style_text_color(report_risk_lbl,D_risk_color(d),0);}
    (void)rs;
    char v[9][64];char sleep_txt[16];
    fmt_min(sleep_txt,sizeof(sleep_txt),sleep_total);
    snprintf(v[0],sizeof(v[0]),"总睡眠  %s",sleep_total?sleep_txt:"待累计");
    snprintf(v[1],sizeof(v[1]),"AHI  %.1f /h",d->ahi);
    snprintf(v[2],sizeof(v[2]),"最低血氧  %d%%",d->min_spo2);
    snprintf(v[3],sizeof(v[3]),"T90  %.1f%%",d->t90_percent);
    snprintf(v[4],sizeof(v[4]),"心率变化  %d bpm",d->delta_hr);
    snprintf(v[5],sizeof(v[5]),"暂停次数  %d 次",d->apnea_count);
    snprintf(v[6],sizeof(v[6]),"低通气数  %d 次",d->hypopnea_count);
    snprintf(v[7],sizeof(v[7]),"呼吸评分  %d",d->snore_resp_score);
    snprintf(v[8],sizeof(v[8]),"最大声  %d dB",d->max_snore_db);
    for(int i=0;i<9;i++)set_label(report_metric_lbl[i],v[i]);
    if(report_eval_lbl&&lv_obj_is_valid(report_eval_lbl)){
        char b[512];
        if(!mic_online&&!radar_online)snprintf(b,sizeof(b),"等待麦克风或对端数据接入...\n请确认 INMP441 已连接, 对端主机通过 UART2 回传 MAIN 报告。");
        else {
            int n = d->snore_type_count[1], t = d->snore_type_count[2];
            int m = d->snore_type_count[3], x = d->snore_type_count[4];
            int total = n+t+m+x;
            uint32_t ns = d->nasal_snore_ms/1000, ts = d->throat_snore_ms/1000;
            uint32_t ms = d->mouth_snore_ms/1000, xs = d->mixed_snore_ms/1000;
            /* 恢复 ST_s 标签 (鼻/喉/口呼吸/混合型/无声) */
            snprintf(b,sizeof(b),
                "声息分析报告\n"
                "当前: %s  置信: %d%%\n"
                "质心: %dHz  低频: %d%%  谐波: %d%%\n"
                "鼻型 %d次 %lus  喉型 %d次 %lus\n"
                "口型 %d次 %lus  混型 %d次 %lus\n"
                "总次数 %d\n"
                "%s\n"
                "仅供参考 非医学诊断",
                ST_s(d->snore_type), d->snore_type_confidence,
                d->spectral_centroid_hz, d->low_freq_ratio_x100, d->harmonic_ratio_x100,
                n, (unsigned long)ns, t, (unsigned long)ts,
                m, (unsigned long)ms, x, (unsigned long)xs,
                total, D_class(d));
        }
        lv_label_set_text(report_eval_lbl,b);
    }
}
static void refresh_event_page(SleepData_t*d,bool radar_online,bool mic_online){
    /* ── 固定演示数据 ── */
    if(event_summary_lbl[0]&&lv_obj_is_valid(event_summary_lbl[0])){
        lv_label_set_text(event_summary_lbl[0],"暂停 0 次  低通气 1 次");
    }
    if(event_summary_lbl[1]&&lv_obj_is_valid(event_summary_lbl[1])){
        lv_label_set_text(event_summary_lbl[1],"AHI 23.1 /h");
    }
    if(event_summary_lbl[2]&&lv_obj_is_valid(event_summary_lbl[2])){
        lv_label_set_text(event_summary_lbl[2],"风险 低");
        lv_obj_set_style_text_color(event_summary_lbl[2],C_GREEN,0);
    }
    if(event_record_lbl&&lv_obj_is_valid(event_record_lbl)){
        lv_label_set_text(event_record_lbl,
            "监测进行中\n\n"
            "低通气事件记录:\n"
            "20:26  低通气  持续23秒  血氧↓4%\n"
            "20:31  低通气  持续18秒  血氧↓3%\n"
            "20:38  低通气  持续31秒  血氧↓5%\n"
            "20:44  低通气  持续15秒  血氧↓2%");
    }
    (void)d;(void)radar_online;(void)mic_online;
}

/* ==================== 刷新 ==================== */
void sleep_ui_refresh(void){
    SleepData_t*d=&g_sleep_data;
    mon=(d->system_state>=SYS_STATE_AUDIO_ONLY);
    bool radar_online = d->sensor.radar_online && !sleep_data_is_timeout(d->sensor.last_radar_update_ms);
    bool mic_online   = d->sensor.mic_online   && !sleep_data_is_timeout(d->sensor.last_mic_update_ms);

    /* ── 状态栏 (纯中文, 避字体缺ASCII) ── */
    if(sb_state&&lv_obj_is_valid(sb_state)){lv_label_set_text(sb_state,Y_s(d->system_state));lv_obj_set_style_text_color(sb_state,Y_c(d->system_state),0);}
    if(sb_wifi&&lv_obj_is_valid(sb_wifi)){
        /* BLE手表连接状态 */
        extern bool watch_ble_is_connected(void);
        bool ble = watch_ble_is_connected();
        lv_label_set_text(sb_wifi,ble?"手表":"--");lv_obj_set_style_text_color(sb_wifi,ble?C_GREEN:C_DIM,0);
    }
    if(sb_radar&&lv_obj_is_valid(sb_radar)){
        if(mic_online){lv_label_set_text(sb_radar,"麦在线");lv_obj_set_style_text_color(sb_radar,C_GREEN,0);}
        else{lv_label_set_text(sb_radar,"麦离线");lv_obj_set_style_text_color(sb_radar,C_RED,0);}
    }
    if(sb_batt&&lv_obj_is_valid(sb_batt)){
        if(d->main_online){lv_label_set_text(sb_batt,"已连接");lv_obj_set_style_text_color(sb_batt,C_GREEN,0);}
        else{lv_label_set_text(sb_batt,"未连接");lv_obj_set_style_text_color(sb_batt,C_RED,0);}
    }
    if(home_arc&&lv_obj_is_valid(home_arc)){
        lv_arc_set_value(home_arc,85);
    }
    if(home_score_val&&lv_obj_is_valid(home_score_val)){
        lv_label_set_text(home_score_val,"85");
    }

    /* ── 呼吸风险 ── */
    if(home_risk_val&&lv_obj_is_valid(home_risk_val)){
        if(d->main_online && !mic_online){
            lv_label_set_text_fmt(home_risk_val,"呼吸风险: %s  (来自主机)",D_risk(d));
            lv_obj_set_style_text_color(home_risk_val,D_risk_color(d),0);
        } else if(!radar_online && mic_online){
            lv_label_set_text(home_risk_val,"主机未连接，风险待评估");
            lv_obj_set_style_text_color(home_risk_val,C_WARN,0);
        } else if(!mic_online){
            lv_label_set_text(home_risk_val,"麦克风: LOW / 检查接线");
            lv_obj_set_style_text_color(home_risk_val,C_RED,0);
        } else {
            lv_label_set_text_fmt(home_risk_val,"呼吸风险: %s",D_risk(d));
            lv_obj_set_style_text_color(home_risk_val,D_risk_color(d),0);
        }
    }

    /* ── 胶囊按钮 ── */
    if(home_capsule_lbl&&lv_obj_is_valid(home_capsule_lbl)){
        if(mon){
            lv_label_set_text(home_capsule_lbl,"监测中 - 点击管理");
            lv_obj_set_style_text_color(home_capsule_lbl,C_BLUE,0);
        }else{
            lv_label_set_text(home_capsule_lbl,"点击开始睡眠监测");
            lv_obj_set_style_text_color(home_capsule_lbl,C_SUB,0);
        }
    }

    /* ── 右卡 OV-Watch 2x2 刷新 ── */
    if(home_dash_detail&&lv_obj_is_valid(home_dash_detail)){
        if(d->main_online)
            lv_label_set_text_fmt(home_dash_detail,"今晚监护中: 呼声%ddB  暂停%d  %s",
                d->snore_db, d->apnea_count,
                d->in_bed?"在床":"离床");
        else if(d->watch_spo2_valid)
            lv_label_set_text_fmt(home_dash_detail,"手表已接入: 血氧%d%%  呼声%ddB", d->watch_spo2, d->snore_db);
        else
            lv_label_set_text(home_dash_detail,"等待主机或手表数据...");
    }
    /* 心率 */
    if(home_card_hr&&lv_obj_is_valid(home_card_hr)){
        if(d->heart_rate>0)
            lv_label_set_text_fmt(home_card_hr,"%d",d->heart_rate);
        else lv_label_set_text(home_card_hr,"--");
    }
    /* 声 dB */
    if(home_card_sn&&lv_obj_is_valid(home_card_sn)){
        if(mic_online && d->snore_db>0) lv_label_set_text_fmt(home_card_sn,"%d",d->snore_db);
        else lv_label_set_text(home_card_sn,"--");
    }
    /* 呼吸率 */
    if(home_card_br&&lv_obj_is_valid(home_card_br)){
        if((d->main_online||radar_online) && d->breath_rate>0)
            lv_label_set_text_fmt(home_card_br,"%d",d->breath_rate);
        else lv_label_set_text(home_card_br,"--");
    }
    /* 血氧 */
    if(home_card_stage&&lv_obj_is_valid(home_card_stage)){
        lv_label_set_text(home_card_stage,"85");
    }

    refresh_monitor_page(d,radar_online,mic_online,false);
    refresh_wave_page(d,radar_online,mic_online);
    refresh_report_page(d,radar_online,mic_online);
    refresh_event_page(d,radar_online,mic_online);
    ai_refresh(d);
}

/* ==================== 初始化 ==================== */
void sleep_ui_init(void){
    sleep_data_init();scr=lv_obj_create(NULL);lv_obj_set_size(scr,LCD_W,LCD_H);lv_obj_set_style_bg_color(scr,C_BG,0);lv_obj_set_style_border_width(scr,0,0);lv_obj_clear_flag(scr,LV_OBJ_FLAG_SCROLLABLE);
    mk_home();mk_monitor();mk_wave();mk_report();mk_event();mk_ai();mk_setting();
    SCB(home_capsule_btn,ev_capsule,LV_EVENT_CLICKED);
    lv_event_cb_t ec[]={ev_mon,ev_wav,ev_rep,ev_evt,ev_ai,ev_set};
    for(int i=0;i<6;i++)SCB(entry_btn[i],ec[i],LV_EVENT_CLICKED);
    for(int i=1;i<PG_CNT;i++){if(pages[i]){lv_obj_t*tp=lv_obj_get_child(pages[i],0);if(tp){lv_obj_t*bk=lv_obj_get_child(tp,0);SCB(bk,ev_back,LV_EVENT_CLICKED);}}}
    setting_hook();
    act_menu_mk();if(act_menu){SCB(act_menu,ev_abg,LV_EVENT_CLICKED);lv_obj_t*bx=lv_obj_get_child(act_menu,0);if(bx){lv_event_cb_t mc[]={ev_as,ev_ap,ev_ae,ev_ad};for(int i=0;i<4;i++){lv_obj_t*btn=lv_obj_get_child(bx,1+i);SCB(btn,mc[i],LV_EVENT_CLICKED);}}}
    cfm_mk();if(cfm_dlg){lv_obj_t*bx=lv_obj_get_child(cfm_dlg,0);if(bx){SCB(lv_obj_get_child(bx,1),ev_cc,LV_EVENT_CLICKED);SCB(lv_obj_get_child(bx,2),ev_co,LV_EVENT_CLICKED);}}
    sleep_player_ui_init(scr); sleep_assistant_ui_init(scr); SCB(home_audio_btn,ev_audio_btn,LV_EVENT_CLICKED);
    control_panel_init(scr);lv_scr_load(scr);sleep_ui_show_home();sleep_ui_refresh();
}
