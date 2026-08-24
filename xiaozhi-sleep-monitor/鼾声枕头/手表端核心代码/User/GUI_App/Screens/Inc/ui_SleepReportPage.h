#ifndef _UI_SLEEP_REPORT_PAGE_H
#define _UI_SLEEP_REPORT_PAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../ui.h"

extern lv_obj_t * ui_SleepReportPage;
extern lv_obj_t * ui_SleepReportTitle;
extern lv_obj_t * ui_SleepReportTotal;
extern lv_obj_t * ui_SleepReportDeep;
extern lv_obj_t * ui_SleepReportLight;
extern lv_obj_t * ui_SleepReportREM;
extern lv_obj_t * ui_SleepReportAwake;
extern lv_obj_t * ui_SleepReportHR;
extern lv_obj_t * ui_SleepReportBackBtn;

extern Page_t Page_SleepReport;

void ui_SleepReportPage_screen_init(void);
void ui_SleepReportPage_screen_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
