#ifndef _UI_SPORTSELECTPAGE_H
#define _UI_SPORTSELECTPAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

extern lv_obj_t * ui_SportSelectPage;
extern lv_obj_t * ui_SportSelTitleLabel;

extern lv_obj_t * ui_SportSelRunPanel;
extern lv_obj_t * ui_SportSelRunButton;
extern lv_obj_t * ui_SportSelRunicon;
extern lv_obj_t * ui_SportSelRunLabel;

extern lv_obj_t * ui_SportSelWalkPanel;
extern lv_obj_t * ui_SportSelWalkButton;
extern lv_obj_t * ui_SportSelWalkicon;
extern lv_obj_t * ui_SportSelWalkLabel;

extern lv_obj_t * ui_SportSelRidePanel;
extern lv_obj_t * ui_SportSelRideButton;
extern lv_obj_t * ui_SportSelRideicon;
extern lv_obj_t * ui_SportSelRideLabel;

extern lv_obj_t * ui_SportSelNoticeLabel;

extern Page_t Page_SportSelect;

void ui_SportSelectPage_screen_init(void);
void ui_SportSelectPage_screen_deinit(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
