#ifndef CONTROL_PANEL_H
#define CONTROL_PANEL_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化控制面板（绑定到指定屏幕对象） */
void control_panel_init(lv_obj_t *parent_scr);

/* 显示/隐藏控制面板 */
void control_panel_show(void);
void control_panel_hide(void);

/* 切换控制面板显示状态 */
void control_panel_toggle(void);

/* 判断控制面板是否显示 */
bool control_panel_is_visible(void);

/* 刷新控制面板状态（WiFi、雷达、麦克风等） */
void control_panel_refresh(void);

#ifdef __cplusplus
}
#endif

#endif
