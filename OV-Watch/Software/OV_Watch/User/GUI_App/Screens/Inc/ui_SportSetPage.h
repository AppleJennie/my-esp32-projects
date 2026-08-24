#ifndef _UI_SPORTSETPAGE_H
#define _UI_SPORTSETPAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

extern lv_obj_t * ui_SportSetPage;

extern Page_t Page_SportSet;

void ui_SportSetPage_screen_init(void);
void ui_SportSetPage_screen_deinit(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
