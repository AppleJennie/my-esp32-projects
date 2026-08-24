#ifndef _UI_SPORTPAGE_H
#define _UI_SPORTPAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

extern lv_obj_t * ui_SportPage;
extern lv_obj_t * ui_SportTypeLabel;
extern lv_obj_t * ui_SportTimeLabel;
extern lv_obj_t * ui_SportHRTitleLabel;
extern lv_obj_t * ui_SportHRValueLabel;
extern lv_obj_t * ui_SportHRZoneLabel;
extern lv_obj_t * ui_SportStepTitleLabel;
extern lv_obj_t * ui_SportStepValueLabel;
extern lv_obj_t * ui_SportKcalTitleLabel;
extern lv_obj_t * ui_SportKcalValueLabel;
extern lv_obj_t * ui_SportPauseBtn;
extern lv_obj_t * ui_SportPauseLabel;
extern lv_obj_t * ui_SportStopBtn;
extern lv_obj_t * ui_SportStopLabel;
extern lv_obj_t * ui_SportOkBtn;
extern lv_obj_t * ui_SportOkLabel;

extern Page_t Page_Sport;

void ui_SportPage_screen_init(void);
void ui_SportPage_screen_deinit(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
