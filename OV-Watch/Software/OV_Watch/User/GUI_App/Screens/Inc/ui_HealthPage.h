#ifndef _UI_HEALTHPAGE_H
#define _UI_HEALTHPAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

extern lv_obj_t * ui_HealthPage;
extern lv_obj_t * ui_HealthTitleLabel;
extern lv_obj_t * ui_HealthStepLabel;
extern lv_obj_t * ui_HealthKcalLabel;
extern lv_obj_t * ui_HealthSportLabel;
extern lv_obj_t * ui_HealthHRLabel;
extern lv_obj_t * ui_HealthHisTitleLabel;

extern Page_t Page_Health;

void ui_HealthPage_screen_init(void);
void ui_HealthPage_screen_deinit(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
