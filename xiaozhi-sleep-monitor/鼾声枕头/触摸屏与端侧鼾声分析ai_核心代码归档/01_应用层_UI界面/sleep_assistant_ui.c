/**
 * @file    sleep_assistant_ui.c
 * @brief   睡眠助手页 — 左滑进入, 6个生活功能卡片
 *          800x480 LVGL v8.3
 */

#include "sleep_assistant_ui.h"
#include "sleep_player_ui.h"
#include "sleep_data.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(lv_font_sleep_ui_16);
LV_FONT_DECLARE(lv_font_montserrat_20);

#define LCD_W 800
#define LCD_H 480
#define PAD   24
#define CR    16
#define GAP   14

#define C_BG     lv_color_hex(0x0D1117)
#define C_CARD   lv_color_hex(0x161B22)
#define C_CARD2  lv_color_hex(0x1C2128)
#define C_BORDER lv_color_hex(0x30363D)
#define C_TXT    lv_color_hex(0xE6EDF3)
#define C_SUB    lv_color_hex(0x8B949E)
#define C_DIM    lv_color_hex(0x484F58)
#define C_BLUE   lv_color_hex(0x58A6FF)
#define C_CYAN   lv_color_hex(0x39C5CF)
#define C_GREEN  lv_color_hex(0x3FB950)
#define C_ORANGE lv_color_hex(0xF0883E)
#define C_RED    lv_color_hex(0xF85149)
#define C_PURPLE lv_color_hex(0xA371F7)

#define F_CN &lv_font_sleep_ui_16
#define F20  &lv_font_montserrat_20

/* ==================== 全局对象 ==================== */
static lv_obj_t *page;
static lv_obj_t *lbl_time;

/* 信息卡片中的动态标签 */
static lv_obj_t *info_weather;
static lv_obj_t *info_indoor;
static lv_obj_t *info_alarm;
static lv_obj_t *info_audio;

/* 子页面 */
static lv_obj_t *sub_page;
static lv_obj_t *sub_title;

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
static void SCB(lv_obj_t *o, lv_event_cb_t cb) {
    if (o && lv_obj_is_valid(o)) lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, NULL);
}

/* ==================== 功能卡片 ==================== */
static lv_obj_t *mk_card(lv_obj_t *p, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                          const char *icon, lv_color_t ic, const char *title, const char *sub) {
    lv_obj_t *c = C(p, w, h); lv_obj_set_pos(c, x, y);

    /* 图标圆 */
    lv_obj_t *o = lv_obj_create(c); lv_obj_set_size(o, 36, 36); lv_obj_set_pos(o, 14, 16);
    lv_obj_set_style_radius(o, 18, 0); lv_obj_set_style_bg_color(o, ic, 0); lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *il = L(o, icon, C_TXT, F20); lv_obj_center(il);

    /* 标题 */
    L(c, title, C_TXT, F_CN); lv_obj_set_pos(lv_obj_get_child(c, lv_obj_get_child_cnt(c)-1), 62, 14);

    /* 副标题 */
    L(c, sub, C_SUB, F_CN); lv_obj_set_pos(lv_obj_get_child(c, lv_obj_get_child_cnt(c)-1), 62, 42);

    return c;
}

/* ==================== 子页面框架 ==================== */
static lv_obj_t *mk_sub(const char *ttl) {
    if (sub_page) lv_obj_del(sub_page);
    sub_page = lv_obj_create(page);
    lv_obj_set_size(sub_page, LCD_W, LCD_H); lv_obj_set_pos(sub_page, 0, 0);
    lv_obj_set_style_bg_color(sub_page, C_BG, 0); lv_obj_set_style_border_width(sub_page, 0, 0);
    lv_obj_set_style_pad_all(sub_page, 0, 0); lv_obj_clear_flag(sub_page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top = lv_obj_create(sub_page);
    lv_obj_set_size(top, LCD_W, 44); lv_obj_set_pos(top, 0, 0);
    lv_obj_set_style_bg_color(top, C_CARD, 0); lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_all(top, 0, 0); lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bk = lv_btn_create(top); lv_obj_set_size(bk, 56, 28); lv_obj_set_pos(bk, 8, 8);
    lv_obj_set_style_radius(bk, 14, 0); lv_obj_set_style_bg_color(bk, C_CARD2, 0);
    lv_obj_set_style_shadow_width(bk, 0, 0); lv_obj_set_style_pad_all(bk, 0, 0);
    L(bk, "返回", C_TXT, F_CN); lv_obj_center(lv_obj_get_child(bk, 0));
    SCB(bk, NULL); /* hooked below */

    sub_title = L(top, ttl, C_TXT, F_CN);
    lv_obj_align(sub_title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *bd = lv_obj_create(sub_page);
    lv_obj_set_size(bd, LCD_W, LCD_H - 44); lv_obj_set_pos(bd, 0, 44);
    lv_obj_set_style_bg_opa(bd, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(bd, 0, 0);
    lv_obj_set_style_pad_all(bd, PAD, 0);
    lv_obj_set_flex_flow(bd, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bd, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(bd, GAP, 0);
    lv_obj_set_scrollbar_mode(bd, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(bd, LV_DIR_VER);
    return bd;
}

static void ev_sub_back(lv_event_t *e) {
    (void)e;
    if (sub_page) lv_obj_del(sub_page);
    sub_page = NULL;
}

/* ==================== 子页面内容 ==================== */
static void show_alarm_page(void) {
    lv_obj_t *bd = mk_sub("智能闹钟设置");
    lv_obj_t *top = lv_obj_get_child(sub_page, 0);
    lv_obj_t *bk = lv_obj_get_child(top, 0);
    SCB(bk, ev_sub_back);

    lv_obj_t *c1 = C(bd, LV_PCT(100), 180);
    L(c1, "闹钟时间", C_SUB, F_CN); lv_obj_set_pos(lv_obj_get_child(c1,0), 16, 10);
    L(c1, "07 : 30", C_TXT, F20); lv_obj_align(lv_obj_get_child(c1,lv_obj_get_child_cnt(c1)-1), LV_ALIGN_CENTER, 0, -10);
    const char *opts[] = {"工作日", "浅睡优先唤醒", "唤醒窗口: 30min", "提示: 轻柔音乐+震动"};
    for (int i = 0; i < 4; i++) {
        L(c1, opts[i], C_TXT, F_CN);
        lv_obj_set_pos(lv_obj_get_child(c1, lv_obj_get_child_cnt(c1)-1), 16, 114 + i*30);
    }

    lv_obj_t *c2 = C(bd, LV_PCT(100), 120);
    L(c2, "智能唤醒说明", C_SUB, F_CN); lv_obj_set_pos(lv_obj_get_child(c2,0), 16, 10);
    L(c2, "基于雷达监测的睡眠阶段数据,\n在设定时间内选择浅睡阶段唤醒。\n避免深睡打断, 起床更轻松。\n待雷达数据稳定后启用。", C_DIM, F_CN);
    lv_obj_set_pos(lv_obj_get_child(c2, lv_obj_get_child_cnt(c2)-1), 16, 34);
    lv_label_set_long_mode(lv_obj_get_child(c2, lv_obj_get_child_cnt(c2)-1), LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lv_obj_get_child(c2, lv_obj_get_child_cnt(c2)-1), LV_PCT(100)-32);
}

static void show_weather_page(void) {
    lv_obj_t *bd = mk_sub("天气与环境");
    lv_obj_t *top = lv_obj_get_child(sub_page, 0);
    lv_obj_t *bk = lv_obj_get_child(top, 0);
    SCB(bk, ev_sub_back);

    SleepData_t *d = &g_sleep_data;
    lv_obj_t *c1 = C(bd, LV_PCT(100), 140);
    L(c1, "室外天气", C_SUB, F_CN); lv_obj_set_pos(lv_obj_get_child(c1,0), 16, 10);
    if (d->sensor.wifi_connected) {
        L(c1, "18C  晴", C_TXT, F20); lv_obj_align(lv_obj_get_child(c1,lv_obj_get_child_cnt(c1)-1), LV_ALIGN_CENTER, 0, -6);
        L(c1, "深圳  轻度污染", C_SUB, F_CN); lv_obj_align(lv_obj_get_child(c1,lv_obj_get_child_cnt(c1)-1), LV_ALIGN_CENTER, 0, 30);
    } else {
        L(c1, "未联网", C_DIM, F_CN); lv_obj_align(lv_obj_get_child(c1,lv_obj_get_child_cnt(c1)-1), LV_ALIGN_CENTER, 0, 10);
    }

    lv_obj_t *c2 = C(bd, LV_PCT(100), 140);
    L(c2, "室内环境", C_SUB, F_CN); lv_obj_set_pos(lv_obj_get_child(c2,0), 16, 10);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.1fC / %.0f%%", d->temperature, d->humidity);
    L(c2, buf, C_TXT, F20); lv_obj_align(lv_obj_get_child(c2,lv_obj_get_child_cnt(c2)-1), LV_ALIGN_CENTER, 0, -6);
    const char *comf = "舒适";
    if (d->comfort == COMFORT_TOO_HOT) comf = "偏热";
    else if (d->comfort == COMFORT_TOO_COLD) comf = "偏冷";
    else if (d->comfort == COMFORT_TOO_DRY) comf = "偏干";
    else if (d->comfort == COMFORT_TOO_HUMID) comf = "偏湿";
    snprintf(buf, sizeof(buf), "体感: %s  光照: 较暗", comf);
    L(c2, buf, C_SUB, F_CN); lv_obj_align(lv_obj_get_child(c2,lv_obj_get_child_cnt(c2)-1), LV_ALIGN_CENTER, 0, 30);
}

static void show_history_page(void) {
    lv_obj_t *bd = mk_sub("历史趋势");
    lv_obj_t *top = lv_obj_get_child(sub_page, 0);
    lv_obj_t *bk = lv_obj_get_child(top, 0);
    SCB(bk, ev_sub_back);

    lv_obj_t *c1 = C(bd, LV_PCT(100), 100);
    L(c1, "最近7天", C_SUB, F_CN); lv_obj_set_pos(lv_obj_get_child(c1,0), 16, 10);
    L(c1, "睡眠评分趋势", C_TXT, F_CN); lv_obj_set_pos(lv_obj_get_child(c1,lv_obj_get_child_cnt(c1)-1), 16, 34);
    lv_obj_t *ca = lv_obj_create(c1);
    lv_obj_set_size(ca, LV_PCT(100)-32, 48);
    lv_obj_set_pos(ca, 16, 60);
    lv_obj_set_style_bg_color(ca, C_CARD2, 0); lv_obj_set_style_border_width(ca, 0, 0);
    lv_obj_clear_flag(ca, LV_OBJ_FLAG_SCROLLABLE);
    L(ca, "图表接入中...", C_DIM, F_CN); lv_obj_center(lv_obj_get_child(ca,0));

    lv_obj_t *c2 = C(bd, LV_PCT(100), 100);
    L(c2, "呼吸风险趋势", C_TXT, F_CN); lv_obj_set_pos(lv_obj_get_child(c2,0), 16, 12);
    lv_obj_t *cb = lv_obj_create(c2);
    lv_obj_set_size(cb, LV_PCT(100)-32, 48);
    lv_obj_set_pos(cb, 16, 40);
    lv_obj_set_style_bg_color(cb, C_CARD2, 0); lv_obj_set_style_border_width(cb, 0, 0);
    lv_obj_clear_flag(cb, LV_OBJ_FLAG_SCROLLABLE);
    L(cb, "图表接入中...", C_DIM, F_CN); lv_obj_center(lv_obj_get_child(cb,0));

    lv_obj_t *c3 = C(bd, LV_PCT(100), 100);
    L(c3, "打呼时长趋势", C_TXT, F_CN); lv_obj_set_pos(lv_obj_get_child(c3,0), 16, 12);
    lv_obj_t *cc = lv_obj_create(c3);
    lv_obj_set_size(cc, LV_PCT(100)-32, 48);
    lv_obj_set_pos(cc, 16, 40);
    lv_obj_set_style_bg_color(cc, C_CARD2, 0); lv_obj_set_style_border_width(cc, 0, 0);
    lv_obj_clear_flag(cc, LV_OBJ_FLAG_SCROLLABLE);
    L(cc, "图表接入中...", C_DIM, F_CN); lv_obj_center(lv_obj_get_child(cc,0));

    lv_obj_t *c4 = C(bd, LV_PCT(100), 60);
    L(c4, "需要至少7晚数据才能生成趋势", C_DIM, F_CN);
    lv_obj_center(lv_obj_get_child(c4,0));
}

static void show_env_page(void) {
    lv_obj_t *bd = mk_sub("睡眠环境");
    lv_obj_t *top = lv_obj_get_child(sub_page, 0);
    lv_obj_t *bk = lv_obj_get_child(top, 0);
    SCB(bk, ev_sub_back);

    SleepData_t *d = &g_sleep_data;
    struct { const char *n; const char *v; } items[] = {
        {"温度", ""}, {"湿度", ""}, {"光照", "较暗"},
        {"环境评分", "85 / 100"}, {"建议", "温湿度适宜, 保持通风"},
    };
    /* fill temp/humidity */
    char tbuf[32], hbuf[32];
    snprintf(tbuf, sizeof(tbuf), "%.1fC", d->temperature);
    snprintf(hbuf, sizeof(hbuf), "%.0f%%", d->humidity);
    items[0].v = tbuf; items[1].v = hbuf;

    lv_obj_t *c1 = C(bd, LV_PCT(100), 220);
    for (int i = 0; i < 5; i++) {
        L(c1, items[i].n, C_SUB, F_CN);
        lv_obj_set_pos(lv_obj_get_child(c1, lv_obj_get_child_cnt(c1)-1), 16, 12+i*40);
        L(c1, items[i].v, C_TXT, F_CN);
        lv_obj_set_pos(lv_obj_get_child(c1, lv_obj_get_child_cnt(c1)-1), 100, 12+i*40);
    }
}

static void show_tools_page(void) {
    lv_obj_t *bd = mk_sub("小工具");
    lv_obj_t *top = lv_obj_get_child(sub_page, 0);
    lv_obj_t *bk = lv_obj_get_child(top, 0);
    SCB(bk, ev_sub_back);

    const char *names[] = {"夜间模式", "屏幕常亮", "倒计时", "帮助说明"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *c = C(bd, LV_PCT(100), 60);
        L(c, names[i], C_TXT, F_CN); lv_obj_set_pos(lv_obj_get_child(c,0), 16, 18);
        L(c, "点击进入", C_SUB, F_CN); lv_obj_set_pos(lv_obj_get_child(c,lv_obj_get_child_cnt(c)-1), LV_PCT(100)-120, 18);
    }
}

/* ==================== 功能卡片点击事件 ==================== */
static void ev_alarm(lv_event_t *e)   { (void)e; show_alarm_page(); }
static void ev_weather(lv_event_t *e) { (void)e; show_weather_page(); }
static void ev_audio(lv_event_t *e)   { (void)e; sleep_player_ui_show(); }
static void ev_history(lv_event_t *e) { (void)e; show_history_page(); }
static void ev_env(lv_event_t *e)     { (void)e; show_env_page(); }
static void ev_tools(lv_event_t *e)   { (void)e; show_tools_page(); }
static void ev_back(lv_event_t *e)    { (void)e; sleep_assistant_ui_hide(); }
static lv_coord_t asst_start_x = 0;
static bool asst_tracking = false;
static void asst_swipe_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t pt; lv_indev_get_point(indev, &pt);
    if (code == LV_EVENT_PRESSED) { asst_start_x = pt.x; asst_tracking = true; }
    else if (code == LV_EVENT_RELEASED && asst_tracking) {
        if (pt.x - asst_start_x > 80) sleep_assistant_ui_hide(); /* 右滑 */
        asst_tracking = false;
    }
}

/* ==================== 构建助手页 ==================== */
void sleep_assistant_ui_init(lv_obj_t *scr) {
    if (!scr) return;

    page = lv_obj_create(scr);
    lv_obj_set_size(page, LCD_W, LCD_H); lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_bg_color(page, C_BG, 0); lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(page, asst_swipe_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(page, asst_swipe_cb, LV_EVENT_RELEASED, NULL);

    /* ---- 顶栏 ---- */
    lv_obj_t *top = lv_obj_create(page);
    lv_obj_set_size(top, LCD_W, 44); lv_obj_set_pos(top, 0, 0);
    lv_obj_set_style_bg_color(top, C_CARD, 0); lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bk = lv_btn_create(top);
    lv_obj_set_size(bk, 56, 28); lv_obj_set_pos(bk, 8, 8);
    lv_obj_set_style_radius(bk, 14, 0); lv_obj_set_style_bg_color(bk, C_CARD2, 0);
    lv_obj_set_style_shadow_width(bk, 0, 0); lv_obj_set_style_pad_all(bk, 0, 0);
    L(bk, "返回", C_TXT, F_CN); lv_obj_center(lv_obj_get_child(bk, 0));
    SCB(bk, ev_back);

    L(top, "睡眠助手", C_TXT, F_CN);
    lv_obj_align(lv_obj_get_child(top, lv_obj_get_child_cnt(top)-1), LV_ALIGN_CENTER, 0, 0);

    lbl_time = L(top, "00:00", C_SUB, F20);
    lv_obj_align(lbl_time, LV_ALIGN_RIGHT_MID, -PAD, 0);

    /* ---- 信息卡片 ---- */
    lv_coord_t y = 54;
    lv_obj_t *info = C(page, LCD_W - 2*PAD, 72);
    lv_obj_set_pos(info, PAD, y);
    lv_obj_set_style_pad_all(info, 14, 0);

    info_weather = L(info, "室外: --", C_SUB, F_CN);
    lv_obj_set_pos(info_weather, 0, 4);
    info_indoor = L(info, "室内: --", C_SUB, F_CN);
    lv_obj_set_pos(info_indoor, 200, 4);
    info_alarm = L(info, "闹钟: 07:30", C_SUB, F_CN);
    lv_obj_set_pos(info_alarm, 0, 36);
    info_audio = L(info, "助眠音: 未播放", C_SUB, F_CN);
    lv_obj_set_pos(info_audio, 200, 36);

    /* ---- 2行 x 3列 功能卡片 ---- */
    y += 72 + GAP;
    lv_coord_t cw = (LCD_W - 2*PAD - 2*GAP) / 3;
    lv_coord_t ch = 150;
    lv_coord_t y2 = y + ch + GAP;

    struct {
        const char *icon; lv_color_t color; const char *title;
        const char *sub; lv_event_cb_t cb;
    } cards[] = {
        {"AL", C_BLUE,   "智能闹钟", "07:30 工作日", ev_alarm},
        {"WE", C_CYAN,   "天气",     "18C 晴",       ev_weather},
        {"AU", C_PURPLE, "助眠音",   "未播放",        ev_audio},
        {"HI", C_GREEN,  "历史趋势", "最近7天",       ev_history},
        {"EN", C_ORANGE, "环境状态", "舒适",          ev_env},
        {"TO", C_SUB,    "小工具",   "夜间/倒计时",   ev_tools},
    };

    for (int i = 0; i < 6; i++) {
        lv_coord_t cx = PAD + (i % 3) * (cw + GAP);
        lv_coord_t cy = (i / 3) ? y2 : y;
        lv_obj_t *cd = mk_card(page, cx, cy, cw, ch, cards[i].icon, cards[i].color, cards[i].title, cards[i].sub);
        SCB(cd, cards[i].cb);
    }
}

/* ==================== 公开 API ==================== */
void sleep_assistant_ui_show(void) {
    if (!page) return;
    /* 更新信息卡片 */
    SleepData_t *d = &g_sleep_data;
    if (info_weather) lv_label_set_text(info_weather, d->sensor.wifi_connected ? "室外: 18C 晴" : "室外: 未联网");
    if (info_indoor) lv_label_set_text_fmt(info_indoor, "室内: %.1fC %.0f%%", d->temperature, d->humidity);
    if (info_audio) lv_label_set_text_fmt(info_audio, "助眠音: %s", sleep_player_get_status_text());
    lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(page);
}

void sleep_assistant_ui_hide(void) {
    if (page) lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    if (sub_page) { lv_obj_del(sub_page); sub_page = NULL; }
}

bool sleep_assistant_ui_is_visible(void) {
    return page && !lv_obj_has_flag(page, LV_OBJ_FLAG_HIDDEN);
}
