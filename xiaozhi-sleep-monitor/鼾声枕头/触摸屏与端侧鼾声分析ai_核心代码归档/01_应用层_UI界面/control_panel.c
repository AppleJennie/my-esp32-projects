#include "control_panel.h"
#include "hardware_control.h"
#include "sleep_data.h"
#include "sleep_player_ui.h"
#include "wifi_user.h"
#include "watch_ble_client.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

/* ===================== Dark Theme ===================== */
#define C_BG            lv_color_hex(0x0D1117)
#define C_CARD          lv_color_hex(0x161B22)
#define C_CARD_LIT      lv_color_hex(0x1C2128)
#define C_BORDER        lv_color_hex(0x21262D)
#define C_TEXT          lv_color_hex(0xE6EDF3)
#define C_SUBTEXT       lv_color_hex(0x8B949E)
#define C_TEXT_DIM      lv_color_hex(0x484F58)

#define C_ACCENT_BLUE   lv_color_hex(0x58A6FF)
#define C_ACCENT_GREEN  lv_color_hex(0x3FB950)
#define C_ACCENT_ORANGE lv_color_hex(0xF0883E)
#define C_ACCENT_RED    lv_color_hex(0xF85149)
#define C_ACCENT_PURPLE lv_color_hex(0xA371F7)
#define C_ACCENT_CYAN   lv_color_hex(0x39C5CF)
#define C_ACCENT_PINK   lv_color_hex(0xF778BA)

#define CP_W            800
#define CP_H            340
#define PAD             12
#define CARD_R          12

LV_FONT_DECLARE(lv_font_sleep_ui_16);
LV_FONT_DECLARE(lv_font_montserrat_20);

#define FONT_CN         &lv_font_sleep_ui_16
#define FONT_NUM20      &lv_font_montserrat_20

/* ===================== UI Objects ===================== */
static lv_obj_t *panel = NULL;
static lv_obj_t *overlay = NULL;
static lv_obj_t *trigger_area = NULL;

/* Toggle button objects (for event identification) */
static lv_obj_t *btn_monitor, *btn_dnd, *btn_wifi, *btn_mute;
static lv_obj_t *btn_keep_screen, *btn_radar, *btn_mic, *btn_night;

/* Toggle labels/icons */
static lv_obj_t *lab_monitor, *lab_dnd, *lab_wifi, *lab_mute;
static lv_obj_t *lab_keep_screen, *lab_radar, *lab_mic, *lab_night;
static lv_obj_t *ico_monitor, *ico_dnd, *ico_wifi, *ico_mute;
static lv_obj_t *ico_keep_screen, *ico_radar, *ico_mic, *ico_night;

/* Sliders */
static lv_obj_t *slider_brightness, *lab_brightness;
static lv_obj_t *slider_volume, *lab_volume;

static bool is_visible = false;

/* Toggle states */
static bool st_monitor  = false;
static bool st_dnd      = false;
static bool st_wifi     = false;
static bool st_mute     = false;
static bool st_keep_scr = false;
static bool st_radar    = true;
static bool st_mic      = true;
static bool st_night    = false;
static int  st_brightness = 70;
static int  st_volume     = 50;

/* Gesture */
static lv_coord_t ges_start_y = 0;
static bool ges_track = false;

/* ===================== Helpers ===================== */
static lv_obj_t *make_label(lv_obj_t *parent, const char *text, lv_color_t color, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_font(l, font, 0);
    return l;
}

/* Apply toggle visual state */
static void toggle_apply(lv_obj_t *btn, lv_obj_t *ico, lv_obj_t *lab,
                          bool on, lv_color_t accent, const char *on_text, const char *off_text)
{
    if (on) {
        lv_obj_set_style_bg_color(btn, accent, 0);
        lv_obj_set_style_border_color(btn, accent, 0);
        lv_obj_set_style_text_color(ico, C_TEXT, 0);
        lv_obj_set_style_text_color(lab, C_TEXT, 0);
        lv_label_set_text(lab, on_text);
    } else {
        lv_obj_set_style_bg_color(btn, C_CARD_LIT, 0);
        lv_obj_set_style_border_color(btn, C_BORDER, 0);
        lv_obj_set_style_text_color(ico, C_SUBTEXT, 0);
        lv_obj_set_style_text_color(lab, C_SUBTEXT, 0);
        lv_label_set_text(lab, off_text);
    }
}

/* ===================== Create Toggle Button ===================== */
static lv_obj_t *create_toggle(lv_obj_t *parent, const char *icon_text, const char *name,
                                lv_obj_t **out_ico, lv_obj_t **out_lab)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 86, 68);
    lv_obj_set_style_radius(btn, CARD_R, 0);
    lv_obj_set_style_bg_color(btn, C_CARD_LIT, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, C_BORDER, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ic = make_label(btn, icon_text, C_SUBTEXT, FONT_NUM20);
    lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *lb = make_label(btn, name, C_SUBTEXT, FONT_CN);
    lv_obj_align(lb, LV_ALIGN_BOTTOM_MID, 0, -6);

    *out_ico = ic;
    *out_lab = lb;
    return btn;
}

/* ===================== Create Slider Row ===================== */
static lv_obj_t *create_slider_row(lv_obj_t *parent, const char *icon_text, int init_val,
                                    lv_obj_t **out_slider, lv_obj_t **out_label)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 380, 52);
    lv_obj_set_style_radius(row, CARD_R, 0);
    lv_obj_set_style_bg_color(row, C_CARD_LIT, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, C_BORDER, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ic = make_label(row, icon_text, C_SUBTEXT, FONT_NUM20);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_set_size(slider, 200, 8);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 0);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, init_val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, C_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, C_ACCENT_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, C_ACCENT_BLUE, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 4, LV_PART_INDICATOR);

    lv_obj_t *val = make_label(row, "70%", C_TEXT, FONT_CN);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, -10, 0);

    *out_slider = slider;
    *out_label = val;
    return row;
}

/* ===================== Animations ===================== */
static void panel_anim_cb(void *var, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)var, v);
}

static void panel_animate_show(void)
{
    if (!panel) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, panel);
    lv_anim_set_values(&a, -CP_H, 0);
    lv_anim_set_time(&a, 280);
    lv_anim_set_exec_cb(&a, panel_anim_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    if (overlay) lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    is_visible = true;
}

static void panel_animate_hide(void)
{
    if (!panel) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, panel);
    lv_anim_set_values(&a, 0, -CP_H);
    lv_anim_set_time(&a, 250);
    lv_anim_set_exec_cb(&a, panel_anim_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_start(&a);
    if (overlay) lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    is_visible = false;
}

/* ===================== Toggle Event Handler ===================== */
static void on_toggle(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    if (btn == btn_monitor) {
        st_monitor = !st_monitor;
        toggle_apply(btn_monitor, ico_monitor, lab_monitor, st_monitor,
                     C_ACCENT_BLUE, "监测中", "监测");
        g_sleep_data.system_state = st_monitor ? SYS_STATE_MONITORING : SYS_STATE_STANDBY;
        hardware_control_send(st_monitor ? HW_CMD_MONITOR_START : HW_CMD_MONITOR_STOP, 0);
    }
    else if (btn == btn_dnd) {
        st_dnd = !st_dnd;
        toggle_apply(btn_dnd, ico_dnd, lab_dnd, st_dnd,
                     C_ACCENT_ORANGE, "已免扰", "免打扰");
        hardware_control_send(st_dnd ? HW_CMD_DND_ON : HW_CMD_DND_OFF, 0);
    }
    else if (btn == btn_wifi) {
        st_wifi = !st_wifi;
        toggle_apply(btn_wifi, ico_wifi, lab_wifi, st_wifi,
                     C_ACCENT_BLUE, "已连接", "WiFi");
        hardware_control_send(st_wifi ? HW_CMD_WIFI_ON : HW_CMD_WIFI_OFF, 0);
    }
    else if (btn == btn_mute) {
        st_mute = !st_mute;
        toggle_apply(btn_mute, ico_mute, lab_mute, st_mute,
                     C_ACCENT_RED, "已静音", "静音");
        hardware_control_send(HW_CMD_SET_VOLUME, st_mute ? 0 : st_volume);
    }
    else if (btn == btn_keep_screen) {
        st_keep_scr = !st_keep_scr;
        toggle_apply(btn_keep_screen, ico_keep_screen, lab_keep_screen, st_keep_scr,
                     C_ACCENT_GREEN, "常亮中", "屏幕常亮");
        hardware_control_send(st_keep_scr ? HW_CMD_KEEP_SCREEN_ON : HW_CMD_KEEP_SCREEN_OFF, 0);
    }
    else if (btn == btn_radar) {
        st_radar = !st_radar;
        toggle_apply(btn_radar, ico_radar, lab_radar, st_radar,
                     C_ACCENT_CYAN, "手表开", "手表");
        if (st_radar) {
            watch_ble_client_start();
        } else {
            watch_ble_client_stop();
        }
    }
    else if (btn == btn_mic) {
        st_mic = !st_mic;
        toggle_apply(btn_mic, ico_mic, lab_mic, st_mic,
                     C_ACCENT_PURPLE, "已开启", "麦克风");
        hardware_control_send(st_mic ? HW_CMD_MIC_ENABLE : HW_CMD_MIC_DISABLE, 0);
    }
    else if (btn == btn_night) {
        st_night = !st_night;
        toggle_apply(btn_night, ico_night, lab_night, st_night,
                     C_ACCENT_PINK, "夜间中", "夜间");
        hardware_control_send(st_night ? HW_CMD_NIGHT_MODE_ON : HW_CMD_NIGHT_MODE_OFF, 0);
    }
}

/* ===================== Slider Events ===================== */
static void on_audio_btn(lv_event_t *e) { (void)e; sleep_player_ui_show(); control_panel_hide(); }
static void on_brightness(lv_event_t *e)
{
    st_brightness = lv_slider_get_value(slider_brightness);
    lv_label_set_text_fmt(lab_brightness, "%d%%", st_brightness);
    hardware_control_send(HW_CMD_SET_BRIGHTNESS, st_brightness);
}

static void on_volume(lv_event_t *e)
{
    st_volume = lv_slider_get_value(slider_volume);
    lv_label_set_text_fmt(lab_volume, "%d%%", st_volume);
    hardware_control_send(HW_CMD_SET_VOLUME, st_volume);
}

/* ===================== Gesture ===================== */
static void trigger_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t vect;
    lv_indev_get_point(indev, &vect);

    if (code == LV_EVENT_PRESSED) {
        ges_start_y = vect.y;
        ges_track = true;
    } else if (code == LV_EVENT_PRESSING && ges_track) {
        if (vect.y - ges_start_y > 40) {
            control_panel_show();
            ges_track = false;
        }
    } else if (code == LV_EVENT_RELEASED) {
        ges_track = false;
    }
}

static void panel_drag_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t vect;
    lv_indev_get_point(indev, &vect);

    if (code == LV_EVENT_PRESSED) {
        ges_start_y = vect.y;
        ges_track = true;
    } else if (code == LV_EVENT_PRESSING && ges_track) {
        if (vect.y - ges_start_y < -40) {
            control_panel_hide();
            ges_track = false;
        }
    } else if (code == LV_EVENT_RELEASED) {
        ges_track = false;
    }
}

static void overlay_click_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        control_panel_hide();
    }
}

/* ===================== Public API ===================== */
void control_panel_init(lv_obj_t *parent_scr)
{
    if (!parent_scr) return;

    /* ---- Overlay ---- */
    overlay = lv_obj_create(parent_scr);
    lv_obj_set_size(overlay, CP_W, 480);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_40, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(overlay, overlay_click_cb, LV_EVENT_CLICKED, NULL);

    /* ---- Panel body ---- */
    panel = lv_obj_create(parent_scr);
    lv_obj_set_size(panel, CP_W, CP_H);
    lv_obj_set_pos(panel, 0, -CP_H);
    lv_obj_set_style_bg_color(panel, C_CARD, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_90, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, PAD, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(panel, panel_drag_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(panel, panel_drag_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(panel, panel_drag_cb, LV_EVENT_RELEASED, NULL);

    /* ---- Drag indicator ---- */
    lv_obj_t *ind = lv_obj_create(panel);
    lv_obj_set_size(ind, 48, 4);
    lv_obj_align(ind, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_radius(ind, 2, 0);
    lv_obj_set_style_bg_color(ind, C_TEXT_DIM, 0);
    lv_obj_set_style_border_width(ind, 0, 0);

    /* ---- Title ---- */
    make_label(panel, "快捷控制", C_TEXT, FONT_CN);
    lv_obj_align(lv_obj_get_child(panel, 1), LV_ALIGN_TOP_MID, 0, 14);

    /* ---- Row 1: Monitor, DND, WiFi, Mute ---- */
    lv_obj_t *row1 = lv_obj_create(panel);
    lv_obj_set_size(row1, CP_W - 2 * PAD, 68);
    lv_obj_align(row1, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row1, 0, 0);
    lv_obj_set_style_pad_all(row1, 0, 0);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row1, LV_OBJ_FLAG_SCROLLABLE);

    btn_monitor = create_toggle(row1, "M", "监测", &ico_monitor, &lab_monitor);
    btn_dnd     = create_toggle(row1, "D", "免打扰", &ico_dnd, &lab_dnd);
    btn_wifi    = create_toggle(row1, "W", "WiFi", &ico_wifi, &lab_wifi);
    btn_mute    = create_toggle(row1, "U", "静音", &ico_mute, &lab_mute);

    /* ---- Row 2: Keep Screen, Radar, Mic, Night ---- */
    lv_obj_t *row2 = lv_obj_create(panel);
    lv_obj_set_size(row2, CP_W - 2 * PAD, 68);
    lv_obj_align(row2, LV_ALIGN_TOP_MID, 0, 118);
    lv_obj_set_style_bg_opa(row2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row2, 0, 0);
    lv_obj_set_style_pad_all(row2, 0, 0);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row2, LV_OBJ_FLAG_SCROLLABLE);

    btn_keep_screen = create_toggle(row2, "L", "屏幕常亮", &ico_keep_screen, &lab_keep_screen);
    btn_radar       = create_toggle(row2, "B", "手表", &ico_radar, &lab_radar);
    btn_mic         = create_toggle(row2, "F", "麦克风", &ico_mic, &lab_mic);
    btn_night       = create_toggle(row2, "N", "夜间", &ico_night, &lab_night);

    /* ---- 助眠音按钮 ---- */
    lv_obj_t *audio_btn = lv_btn_create(panel);
    lv_obj_set_size(audio_btn, CP_W - 2 * PAD, 38);
    lv_obj_align(audio_btn, LV_ALIGN_TOP_MID, 0, 194);
    lv_obj_set_style_radius(audio_btn, CARD_R, 0);
    lv_obj_set_style_bg_color(audio_btn, C_CARD_LIT, 0);
    lv_obj_set_style_border_width(audio_btn, 1, 0);
    lv_obj_set_style_border_color(audio_btn, C_BORDER, 0);
    lv_obj_set_style_shadow_width(audio_btn, 0, 0);
    lv_obj_set_style_pad_all(audio_btn, 0, 0);
    make_label(audio_btn, "助眠音  >", C_TEXT, FONT_CN);
    lv_obj_center(lv_obj_get_child(audio_btn, 0));
    lv_obj_add_event_cb(audio_btn, on_audio_btn, LV_EVENT_CLICKED, NULL);

    /* ---- Sliders ---- */
    lv_obj_t *slider_area = lv_obj_create(panel);
    lv_obj_set_size(slider_area, CP_W - 2 * PAD, 52);
    lv_obj_align(slider_area, LV_ALIGN_BOTTOM_MID, 0, -PAD);
    lv_obj_set_style_bg_opa(slider_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(slider_area, 0, 0);
    lv_obj_set_style_pad_all(slider_area, 0, 0);
    lv_obj_set_flex_flow(slider_area, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(slider_area, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(slider_area, LV_OBJ_FLAG_SCROLLABLE);

    create_slider_row(slider_area, "B", st_brightness, &slider_brightness, &lab_brightness);
    lv_obj_add_event_cb(slider_brightness, on_brightness, LV_EVENT_VALUE_CHANGED, NULL);

    create_slider_row(slider_area, "V", st_volume, &slider_volume, &lab_volume);
    lv_obj_add_event_cb(slider_volume, on_volume, LV_EVENT_VALUE_CHANGED, NULL);

    /* Attach toggle events */
    lv_obj_t *toggles[] = {btn_monitor, btn_dnd, btn_wifi, btn_mute,
                           btn_keep_screen, btn_radar, btn_mic, btn_night};
    for (int i = 0; i < 8; i++) {
        lv_obj_add_event_cb(toggles[i], on_toggle, LV_EVENT_CLICKED, NULL);
    }

    /* ---- Top trigger zone ---- */
    trigger_area = lv_obj_create(parent_scr);
    lv_obj_set_size(trigger_area, CP_W, 10);
    lv_obj_set_pos(trigger_area, 0, 0);
    lv_obj_set_style_bg_opa(trigger_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(trigger_area, 0, 0);
    lv_obj_set_style_pad_all(trigger_area, 0, 0);
    lv_obj_add_flag(trigger_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(trigger_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(trigger_area, trigger_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(trigger_area, trigger_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(trigger_area, trigger_cb, LV_EVENT_RELEASED, NULL);

    /* Sync initial state */
    st_wifi = wifi_is_connected();
    control_panel_refresh();
}

void control_panel_show(void)
{
    if (!is_visible) panel_animate_show();
}

void control_panel_hide(void)
{
    if (is_visible) panel_animate_hide();
}

void control_panel_toggle(void)
{
    is_visible ? control_panel_hide() : control_panel_show();
}

bool control_panel_is_visible(void)
{
    return is_visible;
}

void control_panel_refresh(void)
{
    /* Sync states from external sources */

    /* WiFi */
    st_wifi = wifi_is_connected();
    toggle_apply(btn_wifi, ico_wifi, lab_wifi, st_wifi,
                 C_ACCENT_BLUE, "已连接", "WiFi");

    /* BLE Watch */
    st_radar = watch_ble_is_connected();
    toggle_apply(btn_radar, ico_radar, lab_radar, st_radar,
                 C_ACCENT_CYAN, "已连接", "手表");

    /* Mic */
    st_mic = g_sleep_data.sensor.mic_online;
    toggle_apply(btn_mic, ico_mic, lab_mic, st_mic,
                 C_ACCENT_PURPLE, "已开启", "麦克风");

    /* Monitor */
    st_monitor = (g_sleep_data.system_state == SYS_STATE_MONITORING ||
                  g_sleep_data.system_state == SYS_STATE_SLEEPING);
    toggle_apply(btn_monitor, ico_monitor, lab_monitor, st_monitor,
                 C_ACCENT_BLUE, "监测中", "监测");

    /* Sliders */
    if (slider_brightness && lab_brightness) {
        lv_slider_set_value(slider_brightness, st_brightness, LV_ANIM_OFF);
        lv_label_set_text_fmt(lab_brightness, "%d%%", st_brightness);
    }
    if (slider_volume && lab_volume) {
        lv_slider_set_value(slider_volume, st_volume, LV_ANIM_OFF);
        lv_label_set_text_fmt(lab_volume, "%d%%", st_volume);
    }
}
