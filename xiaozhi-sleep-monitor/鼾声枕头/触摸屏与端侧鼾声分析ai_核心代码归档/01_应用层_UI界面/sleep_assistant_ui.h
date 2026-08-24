#ifndef SLEEP_ASSISTANT_UI_H
#define SLEEP_ASSISTANT_UI_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void sleep_assistant_ui_init(lv_obj_t *parent_scr);
void sleep_assistant_ui_show(void);
void sleep_assistant_ui_hide(void);
bool sleep_assistant_ui_is_visible(void);

#ifdef __cplusplus
}
#endif

#endif
