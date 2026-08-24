/* ============================================
 *  Sleep Start Page - Auto Monitor Display
 *  No manual start/stop button. Monitoring
 *  starts automatically at night (22:00 ~ 08:00).
 * ============================================ */
#include "ui.h"
#include "ui_SleepStartPage.h"
#include "user_SleepMonitorTask.h"
#include "mpu6050.h"
#include "rtc.h"
#include "ui_HomePage.h"
#include "user_TasksInit.h"

/* Private variables */
lv_obj_t * ui_SleepStartPage;
static lv_obj_t * ui_SleepStartPosture;
static lv_obj_t * ui_SleepStartTurns;
static lv_obj_t * ui_SleepStartStatus;
static lv_obj_t * ui_SleepStartTimeHint;
static lv_timer_t * SleepStart_timer;

/* Forward declarations */
static void SleepStart_timer_cb(lv_timer_t * timer);
static void ui_event_ReturnBtn(lv_event_t * e);

/* ======================= Page Definition ======================= */

Page_t Page_SleepStart = {
    ui_SleepStartPage_screen_init,
    ui_SleepStartPage_screen_deinit,
    &ui_SleepStartPage,
};

/* ======================= Timer Callback ======================= */

static void SleepStart_timer_cb(lv_timer_t * timer)
{
    char buf[64];

    /* Posture (read with scheduler suspended to avoid race) */
    vTaskSuspendAll();
    uint8_t posture = MPU_Get_SleepPosture();
    const char * posture_name = MPU_Get_PostureName(posture);
    xTaskResumeAll();
    sprintf(buf, "睡姿: %s", posture_name);
    lv_label_set_text(ui_SleepStartPosture, buf);

    /* Turns */
    uint16_t turns = SleepMonitor_GetTotalPostureChanges();
    sprintf(buf, "翻身: %d", turns);
    lv_label_set_text(ui_SleepStartTurns, buf);

    /* Status line: show if monitoring active + epoch count */
    uint16_t ec = SleepMonitor_GetEpochCount();
    if (SleepMonitor_IsActive()) {
        sprintf(buf, "监测中  %d epochs", ec);
    } else {
        sprintf(buf, "未监测");
    }
    lv_label_set_text(ui_SleepStartStatus, buf);
}

/* ======================= UI Event Handlers ======================= */

static void ui_event_ReturnBtn(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED) {
        Page_Load(&Page_Home);
    }
}

/* ======================= Screen Init ======================= */

void ui_SleepStartPage_screen_init(void)
{
    ui_SleepStartPage = lv_obj_create(NULL);
    lv_obj_set_size(ui_SleepStartPage, 240, 280);
    lv_obj_clear_flag(ui_SleepStartPage, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_SleepStartPage, lv_color_hex(0x000000), LV_PART_MAIN);

    /* === Title === */
    lv_obj_t * title = lv_label_create(ui_SleepStartPage);
    lv_obj_set_pos(title, 60, 20);
    lv_label_set_text(title, "睡眠监测");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &ui_font_Cuyuan20, LV_PART_MAIN);

    /* === Posture Display === */
    ui_SleepStartPosture = lv_label_create(ui_SleepStartPage);
    lv_obj_set_pos(ui_SleepStartPosture, 40, 80);
    lv_label_set_text(ui_SleepStartPosture, "睡姿: --");
    lv_obj_set_style_text_color(ui_SleepStartPosture, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_SleepStartPosture, &ui_font_Cuyuan20, LV_PART_MAIN);

    /* === Turns Display === */
    ui_SleepStartTurns = lv_label_create(ui_SleepStartPage);
    lv_obj_set_pos(ui_SleepStartTurns, 40, 120);
    lv_label_set_text(ui_SleepStartTurns, "翻身: 0");
    lv_obj_set_style_text_color(ui_SleepStartTurns, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_SleepStartTurns, &ui_font_Cuyuan20, LV_PART_MAIN);

    /* === Status Line === */
    ui_SleepStartStatus = lv_label_create(ui_SleepStartPage);
    lv_obj_set_pos(ui_SleepStartStatus, 40, 170);
    lv_label_set_text(ui_SleepStartStatus, "未监测");
    lv_obj_set_style_text_color(ui_SleepStartStatus, lv_color_hex(0x00FF00), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_SleepStartStatus, &ui_font_Cuyuan20, LV_PART_MAIN);

    /* === Time Hint === */
    ui_SleepStartTimeHint = lv_label_create(ui_SleepStartPage);
    lv_obj_set_pos(ui_SleepStartTimeHint, 30, 210);
    lv_label_set_text(ui_SleepStartTimeHint, "自动监测: 22:00~08:00");
    lv_obj_set_style_text_color(ui_SleepStartTimeHint, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_SleepStartTimeHint, &ui_font_Cuyuan20, LV_PART_MAIN);

    /* === Return Button === */
    lv_obj_t * return_btn = lv_btn_create(ui_SleepStartPage);
    lv_obj_set_size(return_btn, 80, 40);
    lv_obj_set_pos(return_btn, 80, 240);
    lv_obj_add_event_cb(return_btn, ui_event_ReturnBtn, LV_EVENT_CLICKED, NULL);

    lv_obj_t * return_label = lv_label_create(return_btn);
    lv_label_set_text(return_label, "返回");
    lv_obj_center(return_label);
    lv_obj_set_style_text_font(return_label, &ui_font_Cuyuan20, LV_PART_MAIN);
    lv_obj_set_style_text_color(return_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

    /* === Start refresh timer === */
    SleepStart_timer = lv_timer_create(SleepStart_timer_cb, 1000, NULL);
    SleepStart_timer_cb(SleepStart_timer); /* Run once immediately */
}

/* ======================= Screen Deinit ======================= */

void ui_SleepStartPage_screen_deinit(void)
{
    if (SleepStart_timer) {
        lv_timer_del(SleepStart_timer);
        SleepStart_timer = NULL;
    }
    lv_obj_del(ui_SleepStartPage);
    ui_SleepStartPage = NULL;
}
