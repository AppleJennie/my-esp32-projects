/**
 * @file    sleep_player_ui.c
 * @brief   助眠播放器 — 白噪声/雨声/海浪/森林/轻音乐
 *          800x480 LVGL v8.3
 */

#include "sleep_player_ui.h"
#include "hardware_control.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(lv_font_sleep_ui_16);
LV_FONT_DECLARE(lv_font_montserrat_48);
LV_FONT_DECLARE(lv_font_montserrat_20);

#define LCD_W 800
#define LCD_H 480
#define PAD   24
#define CR    16
#define GAP   12

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
#define F48  &lv_font_montserrat_48
#define F20  &lv_font_montserrat_20

/* ==================== 声音定义 ==================== */
static const char *snd_names[] = {"雨声入眠", "海浪轻拍", "白噪声", "森林鸟鸣", "轻音乐"};
static const char *snd_eng[]   = {"Rain Sleep", "Ocean Waves", "White Noise", "Forest", "Lullaby"};
static const char *snd_paths[] = {
    "/0:/sleep_audio/rain.wav",
    "/0:/sleep_audio/ocean.wav",
    "/0:/sleep_audio/white_noise.wav",
    "/0:/sleep_audio/forest.wav",
    "/0:/sleep_audio/lullaby.wav",
};
#define SND_COUNT 5

/* 定时选项: 分钟数, 0=整晚 */
static int timer_opts[] = {15, 30, 60, 0};
static const char *timer_names[] = {"15min", "30min", "60min", "整晚"};
#define TIMER_COUNT 4

/* ==================== 播放器状态 ==================== */
static int cur_sound = 0;       /* 0-4 */
static bool playing = false;
static int volume = 50;
static int timer_idx = 1;       /* 默认30min */
static uint32_t timer_end_ms = 0;
static lv_timer_t *countdown_timer = NULL;

/* ==================== UI 对象 ==================== */
static lv_obj_t *page;
static lv_obj_t *disc_arc;
static lv_obj_t *disc_name;
static lv_obj_t *disc_eng;
static lv_obj_t *btn_play;
static lv_obj_t *lbl_play;
static lv_obj_t *lbl_timer;
static lv_obj_t *timer_btns[TIMER_COUNT];
static lv_obj_t *slider_vol;
static lv_obj_t *lbl_vol;
static lv_obj_t *sound_btns[SND_COUNT];
static lv_obj_t *lbl_time;

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

/* ==================== 定时器回调 ==================== */
static void countdown_cb(lv_timer_t *t) {
    if (!playing || timer_idx == 3) { /* 整晚模式不计时 */
        if (lbl_timer) lv_label_set_text(lbl_timer, "整晚播放");
        return;
    }
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now >= timer_end_ms) {
        playing = false;
        if (lbl_play) lv_label_set_text(lbl_play, "播放");
        if (lbl_timer) lv_label_set_text(lbl_timer, "定时结束");
        if (countdown_timer) { lv_timer_del(countdown_timer); countdown_timer = NULL; }
        return;
    }
    uint32_t remain = (timer_end_ms - now) / 1000;
    char buf[32];
    snprintf(buf, sizeof(buf), "剩余 %02lu:%02lu", (unsigned long)(remain/60), (unsigned long)(remain%60));
    if (lbl_timer) lv_label_set_text(lbl_timer, buf);
}

/* ==================== 启动倒计时 ==================== */
static void timer_start(void) {
    if (countdown_timer) { lv_timer_del(countdown_timer); countdown_timer = NULL; }
    if (timer_idx == 3) { /* 整晚 */
        if (lbl_timer) lv_label_set_text(lbl_timer, "整晚播放");
        return;
    }
    uint32_t ms = (uint32_t)timer_opts[timer_idx] * 60 * 1000;
    timer_end_ms = (uint32_t)(esp_timer_get_time() / 1000) + ms;
    countdown_timer = lv_timer_create(countdown_cb, 1000, NULL);
}

/* ==================== 更新 UI ==================== */
static void ui_update(void) {
    /* 播放盘 */
    if (disc_arc) lv_arc_set_value(disc_arc, playing ? (timer_idx==3?85:70) : 0);
    if (disc_name) { lv_label_set_text(disc_name, snd_names[cur_sound]); lv_obj_set_style_text_color(disc_name, playing?C_TXT:C_SUB,0); }
    if (disc_eng) lv_label_set_text(disc_eng, snd_eng[cur_sound]);

    /* 播放按钮 */
    if (lbl_play) lv_label_set_text(lbl_play, playing ? "暂停" : "播放");

    /* 定时按钮高亮 */
    for (int i = 0; i < TIMER_COUNT; i++) {
        if (timer_btns[i]) {
            lv_obj_set_style_bg_color(timer_btns[i], (i==timer_idx)?C_BLUE:C_CARD2, 0);
            lv_obj_set_style_text_color(lv_obj_get_child(timer_btns[i],0), (i==timer_idx)?C_TXT:C_SUB, 0);
        }
    }

    /* 声音按钮高亮 */
    for (int i = 0; i < SND_COUNT; i++) {
        if (sound_btns[i]) {
            lv_obj_set_style_bg_color(sound_btns[i], (i==cur_sound)?C_BLUE:C_CARD2, 0);
            lv_obj_set_style_border_color(sound_btns[i], (i==cur_sound)?C_BLUE:C_BORDER, 0);
            lv_obj_set_style_text_color(lv_obj_get_child(sound_btns[i],0), (i==cur_sound)?C_TXT:C_SUB, 0);
        }
    }

    /* 音量 */
    if (slider_vol) lv_slider_set_value(slider_vol, volume, LV_ANIM_OFF);
    if (lbl_vol) lv_label_set_text_fmt(lbl_vol, "%d%%", volume);
}

/* ==================== 事件回调 ==================== */
static void ev_play(lv_event_t *e) {
    (void)e;
    if (playing) {
        playing = false;
        hardware_control_send(HW_CMD_SET_VOLUME, 0);
        ESP_LOGI("PLAYER", "pause %s", snd_names[cur_sound]);
        if (countdown_timer) { lv_timer_del(countdown_timer); countdown_timer = NULL; }
    } else {
        playing = true;
        hardware_control_send(HW_CMD_SET_VOLUME, volume);
        ESP_LOGI("PLAYER", "play %s", snd_names[cur_sound]);
        timer_start();
    }
    ui_update();
}

static void ev_prev(lv_event_t *e) {
    (void)e;
    cur_sound = (cur_sound + SND_COUNT - 1) % SND_COUNT;
    playing = false;
    if (countdown_timer) { lv_timer_del(countdown_timer); countdown_timer = NULL; }
    ESP_LOGI("PLAYER", "switch to %s", snd_names[cur_sound]);
    ui_update();
}

static void ev_next(lv_event_t *e) {
    (void)e;
    cur_sound = (cur_sound + 1) % SND_COUNT;
    playing = false;
    if (countdown_timer) { lv_timer_del(countdown_timer); countdown_timer = NULL; }
    ESP_LOGI("PLAYER", "switch to %s", snd_names[cur_sound]);
    ui_update();
}

static void ev_timer(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    for (int i = 0; i < TIMER_COUNT; i++) {
        if (btn == timer_btns[i]) { timer_idx = i; break; }
    }
    if (playing) timer_start();
    ui_update();
}

static void ev_vol(lv_event_t *e) {
    volume = lv_slider_get_value(slider_vol);
    if (lbl_vol) lv_label_set_text_fmt(lbl_vol, "%d%%", volume);
    if (playing) hardware_control_send(HW_CMD_SET_VOLUME, volume);
}

static void ev_sound(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    for (int i = 0; i < SND_COUNT; i++) {
        if (btn == sound_btns[i]) { cur_sound = i; break; }
    }
    playing = false;
    if (countdown_timer) { lv_timer_del(countdown_timer); countdown_timer = NULL; }
    ESP_LOGI("PLAYER", "select %s", snd_names[cur_sound]);
    ui_update();
}

static void ev_back(lv_event_t *e) { (void)e; sleep_player_ui_hide(); }

/* ==================== 构建页面 ==================== */
void sleep_player_ui_init(lv_obj_t *scr) {
    if (!scr) return;

    page = lv_obj_create(scr);
    lv_obj_set_size(page, LCD_W, LCD_H);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_bg_color(page, C_BG, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- 顶栏 ---- */
    lv_obj_t *top = lv_obj_create(page);
    lv_obj_set_size(top, LCD_W, 44);
    lv_obj_set_pos(top, 0, 0);
    lv_obj_set_style_bg_color(top, C_CARD, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bk = lv_btn_create(top);
    lv_obj_set_size(bk, 56, 28); lv_obj_set_pos(bk, 8, 8);
    lv_obj_set_style_radius(bk, 14, 0); lv_obj_set_style_bg_color(bk, C_CARD2, 0);
    lv_obj_set_style_shadow_width(bk, 0, 0); lv_obj_set_style_pad_all(bk, 0, 0);
    L(bk, "返回", C_TXT, F_CN); lv_obj_center(lv_obj_get_child(bk, 0));
    SCB(bk, ev_back);

    L(top, "助眠播放器", C_TXT, F_CN);
    lv_obj_align(lv_obj_get_child(top, lv_obj_get_child_cnt(top)-1), LV_ALIGN_CENTER, 0, 0);

    lbl_time = L(top, "00:00", C_SUB, F20);
    lv_obj_align(lbl_time, LV_ALIGN_RIGHT_MID, -PAD, 0);

    /* ---- 播放盘区域 ---- */
    lv_coord_t disc_y = 56;

    /* 外圈 arc */
    disc_arc = lv_arc_create(page);
    lv_obj_set_size(disc_arc, 180, 180);
    lv_obj_align(disc_arc, LV_ALIGN_TOP_MID, 0, disc_y);
    lv_arc_set_range(disc_arc, 0, 100);
    lv_arc_set_value(disc_arc, 0);
    lv_arc_set_bg_angles(disc_arc, 0, 360);
    lv_obj_set_style_arc_color(disc_arc, C_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(disc_arc, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(disc_arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(disc_arc, 8, LV_PART_MAIN);
    lv_obj_remove_style(disc_arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(disc_arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(disc_arc, 0, 0);

    /* 内圈实心背景 */
    lv_obj_t *inner = lv_obj_create(page);
    lv_obj_set_size(inner, 120, 120);
    lv_obj_align(inner, LV_ALIGN_TOP_MID, 0, disc_y+30);
    lv_obj_set_style_radius(inner, 60, 0);
    lv_obj_set_style_bg_color(inner, C_CARD2, 0);
    lv_obj_set_style_border_width(inner, 1, 0);
    lv_obj_set_style_border_color(inner, C_BORDER, 0);
    lv_obj_clear_flag(inner, LV_OBJ_FLAG_SCROLLABLE);

    disc_name = L(inner, "雨声入眠", C_SUB, F_CN);
    lv_obj_align(disc_name, LV_ALIGN_CENTER, 0, -10);
    disc_eng = L(inner, "Rain Sleep", C_DIM, F20);
    lv_obj_align(disc_eng, LV_ALIGN_CENTER, 0, 14);

    /* ---- 控制按钮 (prev / play / next) ---- */
    lv_coord_t ctrl_y = disc_y + 190;
    lv_obj_t *ctrl_bar = lv_obj_create(page);
    lv_obj_set_size(ctrl_bar, 300, 52);
    lv_obj_align(ctrl_bar, LV_ALIGN_TOP_MID, 0, ctrl_y);
    lv_obj_set_style_bg_opa(ctrl_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctrl_bar, 0, 0);
    lv_obj_set_style_pad_all(ctrl_bar, 0, 0);
    lv_obj_set_flex_flow(ctrl_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl_bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(ctrl_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bp = lv_btn_create(ctrl_bar);
    lv_obj_set_size(bp, 60, 44); lv_obj_set_style_radius(bp, 12, 0);
    lv_obj_set_style_bg_color(bp, C_CARD2, 0); lv_obj_set_style_shadow_width(bp, 0, 0);
    L(bp, "上一首", C_TXT, F_CN); lv_obj_center(lv_obj_get_child(bp, 0));
    SCB(bp, ev_prev);

    btn_play = lv_btn_create(ctrl_bar);
    lv_obj_set_size(btn_play, 80, 44); lv_obj_set_style_radius(btn_play, 12, 0);
    lv_obj_set_style_bg_color(btn_play, C_BLUE, 0); lv_obj_set_style_shadow_width(btn_play, 0, 0);
    lbl_play = L(btn_play, "播放", C_TXT, F_CN); lv_obj_center(lbl_play);
    SCB(btn_play, ev_play);

    lv_obj_t *bn = lv_btn_create(ctrl_bar);
    lv_obj_set_size(bn, 60, 44); lv_obj_set_style_radius(bn, 12, 0);
    lv_obj_set_style_bg_color(bn, C_CARD2, 0); lv_obj_set_style_shadow_width(bn, 0, 0);
    L(bn, "下一首", C_TXT, F_CN); lv_obj_center(lv_obj_get_child(bn, 0));
    SCB(bn, ev_next);

    /* ---- 定时选项 ---- */
    lv_coord_t timer_y = ctrl_y + 60;
    lv_obj_t *timer_row = lv_obj_create(page);
    lv_obj_set_size(timer_row, 500, 38);
    lv_obj_align(timer_row, LV_ALIGN_TOP_MID, 0, timer_y);
    lv_obj_set_style_bg_opa(timer_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(timer_row, 0, 0);
    lv_obj_set_style_pad_all(timer_row, 0, 0);
    lv_obj_set_flex_flow(timer_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(timer_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(timer_row, LV_OBJ_FLAG_SCROLLABLE);

    lbl_timer = L(page, "剩余 30:00", C_SUB, F_CN);
    lv_obj_align(lbl_timer, LV_ALIGN_TOP_MID, 0, timer_y - 20);

    for (int i = 0; i < TIMER_COUNT; i++) {
        timer_btns[i] = lv_btn_create(timer_row);
        lv_obj_set_size(timer_btns[i], 100, 32);
        lv_obj_set_style_radius(timer_btns[i], 16, 0);
        lv_obj_set_style_bg_color(timer_btns[i], C_CARD2, 0);
        lv_obj_set_style_shadow_width(timer_btns[i], 0, 0);
        lv_obj_set_style_border_width(timer_btns[i], 0, 0);
        L(timer_btns[i], timer_names[i], C_SUB, F_CN);
        lv_obj_center(lv_obj_get_child(timer_btns[i], 0));
        SCB(timer_btns[i], ev_timer);
    }

    /* ---- 音量 ---- */
    lv_coord_t vol_y = timer_y + 55;
    lv_obj_t *vol_row = lv_obj_create(page);
    lv_obj_set_size(vol_row, LCD_W - 2*PAD, 44);
    lv_obj_align(vol_row, LV_ALIGN_TOP_MID, 0, vol_y);
    lv_obj_set_style_bg_color(vol_row, C_CARD, 0);
    lv_obj_set_style_border_width(vol_row, 1, 0);
    lv_obj_set_style_border_color(vol_row, C_BORDER, 0);
    lv_obj_set_style_radius(vol_row, CR, 0);
    lv_obj_set_style_pad_all(vol_row, 0, 0);
    lv_obj_clear_flag(vol_row, LV_OBJ_FLAG_SCROLLABLE);

    L(vol_row, "音量", C_SUB, F_CN);
    lv_obj_align(lv_obj_get_child(vol_row, 0), LV_ALIGN_LEFT_MID, 14, 0);

    slider_vol = lv_slider_create(vol_row);
    lv_obj_set_size(slider_vol, 400, 8);
    lv_obj_align(slider_vol, LV_ALIGN_CENTER, 0, 0);
    lv_slider_set_range(slider_vol, 0, 100);
    lv_slider_set_value(slider_vol, volume, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider_vol, C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_vol, C_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_vol, C_BLUE, LV_PART_KNOB);
    lv_obj_add_event_cb(slider_vol, ev_vol, LV_EVENT_VALUE_CHANGED, NULL);

    lbl_vol = L(vol_row, "50%", C_TXT, F_CN);
    lv_obj_align(lbl_vol, LV_ALIGN_RIGHT_MID, -14, 0);

    /* ---- 声音选择 ---- */
    lv_coord_t snd_y = vol_y + 54;
    lv_obj_t *snd_row = lv_obj_create(page);
    lv_obj_set_size(snd_row, LCD_W - 2*PAD, 50);
    lv_obj_align(snd_row, LV_ALIGN_TOP_MID, 0, snd_y);
    lv_obj_set_style_bg_opa(snd_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(snd_row, 0, 0);
    lv_obj_set_style_pad_all(snd_row, 0, 0);
    lv_obj_set_flex_flow(snd_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(snd_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(snd_row, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < SND_COUNT; i++) {
        sound_btns[i] = lv_btn_create(snd_row);
        lv_obj_set_size(sound_btns[i], 130, 44);
        lv_obj_set_style_radius(sound_btns[i], 12, 0);
        lv_obj_set_style_bg_color(sound_btns[i], C_CARD2, 0);
        lv_obj_set_style_border_width(sound_btns[i], 1, 0);
        lv_obj_set_style_border_color(sound_btns[i], C_BORDER, 0);
        lv_obj_set_style_shadow_width(sound_btns[i], 0, 0);
        L(sound_btns[i], snd_names[i], C_SUB, F_CN);
        lv_obj_center(lv_obj_get_child(sound_btns[i], 0));
        SCB(sound_btns[i], ev_sound);
    }

    ui_update();
}

/* ==================== 公开 API ==================== */
void sleep_player_ui_show(void) {
    if (page) { lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(page); }
}

void sleep_player_ui_hide(void) {
    if (page) lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    if (playing) {
        playing = false;
        if (countdown_timer) { lv_timer_del(countdown_timer); countdown_timer = NULL; }
        hardware_control_send(HW_CMD_SET_VOLUME, 0);
    }
}

bool sleep_player_ui_is_visible(void) {
    return page && !lv_obj_has_flag(page, LV_OBJ_FLAG_HIDDEN);
}

const char *sleep_player_get_status_text(void) {
    static char buf[64];
    if (playing) {
        snprintf(buf, sizeof(buf), "%s - 播放中", snd_names[cur_sound]);
    } else {
        snprintf(buf, sizeof(buf), "未播放");
    }
    return buf;
}
