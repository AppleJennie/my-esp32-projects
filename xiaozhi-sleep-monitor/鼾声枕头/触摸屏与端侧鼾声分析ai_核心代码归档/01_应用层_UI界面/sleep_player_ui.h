#ifndef SLEEP_PLAYER_UI_H
#define SLEEP_PLAYER_UI_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void sleep_player_ui_init(lv_obj_t *parent_scr);
void sleep_player_ui_show(void);
void sleep_player_ui_hide(void);
bool sleep_player_ui_is_visible(void);

/* 外部查询播放器状态（供助手页/首页显示） */
const char *sleep_player_get_status_text(void);  /* "未播放" / "雨声 - 23min" */

#ifdef __cplusplus
}
#endif

#endif
