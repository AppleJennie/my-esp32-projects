#ifndef _UI_REMINDPAGE_H
#define _UI_REMINDPAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

#define REMIND_SEDENTARY  0
#define REMIND_SPORT      1

extern lv_obj_t * ui_RemindPage;

//set before Page_Load(&Page_Remind): REMIND_SEDENTARY or REMIND_SPORT
extern uint8_t ui_RemindType;

extern Page_t Page_Remind;

void ui_RemindPage_screen_init(void);
void ui_RemindPage_screen_deinit(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
