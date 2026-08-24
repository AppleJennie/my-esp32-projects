#ifndef SLEEP_UI_H
#define SLEEP_UI_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void sleep_ui_init(void);
void sleep_ui_refresh(void);

/* 页面切换 */
void sleep_ui_show_home(void);
void sleep_ui_show_monitor(void);
void sleep_ui_show_wave(void);
void sleep_ui_show_report(void);
void sleep_ui_show_event(void);
void sleep_ui_show_ai(void);
void sleep_ui_show_setting(void);
void sleep_ui_show_assistant(void);
void sleep_ui_show_player(void);

/* 状态栏时间更新 */
void sleep_ui_update_time(int hour, int min, int sec);

/* 系统状态控制 */
void sleep_ui_set_monitoring(bool on);
void sleep_ui_set_dnd(bool on);
void sleep_ui_set_night_mode(bool on);
void sleep_ui_set_keep_screen_on(bool on);
void sleep_ui_set_mic_enabled(bool on);
void sleep_ui_set_radar_enabled(bool on);
void sleep_ui_set_mute(bool on);

int sleep_ui_get_current_page(void);

#ifdef __cplusplus
}
#endif

#endif
