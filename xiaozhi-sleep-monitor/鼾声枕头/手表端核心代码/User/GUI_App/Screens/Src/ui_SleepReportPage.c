#include "../../ui.h"
#include "../../ui_helpers.h"
#include "../Inc/ui_SleepReportPage.h"
#include "../../../Func/Inc/HWDataAccess.h"
#include "../../../Tasks/Inc/user_SleepMonitorTask.h"
#include "../../../Func/Inc/SleepRecord.h"

Page_t Page_SleepReport = {ui_SleepReportPage_screen_init, ui_SleepReportPage_screen_deinit, &ui_SleepReportPage};

lv_obj_t * ui_SleepReportPage;
lv_obj_t * ui_SleepReportTitle;
lv_obj_t * ui_SleepReportTotal;
lv_obj_t * ui_SleepReportDeep;
lv_obj_t * ui_SleepReportLight;
lv_obj_t * ui_SleepReportREM;
lv_obj_t * ui_SleepReportAwake;
lv_obj_t * ui_SleepReportHR;
lv_obj_t * ui_SleepReportBackBtn;

static void ui_event_SleepReportPage(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if(event_code == LV_EVENT_GESTURE)
    {
        if(lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT)
        {
            Page_Back_Bottom();
        }
    }
}

static void ui_event_SleepReportBackBtn(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if(event_code == LV_EVENT_CLICKED)
    {
        Page_Back_Bottom();
    }
}

void ui_SleepReportPage_screen_init(void)
{
    char buf[64];
    SleepSummary_t sum;
    const SleepSummary_t *live = SleepMonitor_GetSummary();

    /* Prefer live summary if available, else load from EEPROM */
    if (live && live->total_min > 0) {
        sum = *live;
    } else {
        if (!SleepRecord_LoadLatest(&sum)) {
            memset(&sum, 0, sizeof(sum));
        }
    }

    ui_SleepReportPage = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_SleepReportPage, LV_OBJ_FLAG_SCROLLABLE);

    /* Title */
    ui_SleepReportTitle = lv_label_create(ui_SleepReportPage);
    lv_obj_set_width(ui_SleepReportTitle, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SleepReportTitle, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_SleepReportTitle, LV_ALIGN_TOP_MID);
    lv_obj_set_y(ui_SleepReportTitle, 10);
    lv_label_set_text(ui_SleepReportTitle, "睡眠报告");
    lv_obj_set_style_text_font(ui_SleepReportTitle, &ui_font_Cuyuan20, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Details */
    ui_SleepReportTotal = lv_label_create(ui_SleepReportPage);
    lv_obj_set_width(ui_SleepReportTotal, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SleepReportTotal, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_SleepReportTotal, LV_ALIGN_TOP_LEFT);
    lv_obj_set_x(ui_SleepReportTotal, 20);
    lv_obj_set_y(ui_SleepReportTotal, 55);
    sprintf(buf, "总睡眠: %dh%02dm", sum.total_min / 60, sum.total_min % 60);
    lv_label_set_text(ui_SleepReportTotal, buf);
    lv_obj_set_style_text_font(ui_SleepReportTotal, &ui_font_Cuyuan20, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_SleepReportDeep = lv_label_create(ui_SleepReportPage);
    lv_obj_set_width(ui_SleepReportDeep, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SleepReportDeep, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_SleepReportDeep, LV_ALIGN_TOP_LEFT);
    lv_obj_set_x(ui_SleepReportDeep, 20);
    lv_obj_set_y(ui_SleepReportDeep, 85);
    sprintf(buf, "深睡: %dh%02dm", sum.deep_min / 60, sum.deep_min % 60);
    lv_label_set_text(ui_SleepReportDeep, buf);
    lv_obj_set_style_text_font(ui_SleepReportDeep, &ui_font_Cuyuan20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_SleepReportDeep, lv_color_hex(0x3F51B5), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_SleepReportLight = lv_label_create(ui_SleepReportPage);
    lv_obj_set_width(ui_SleepReportLight, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SleepReportLight, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_SleepReportLight, LV_ALIGN_TOP_LEFT);
    lv_obj_set_x(ui_SleepReportLight, 20);
    lv_obj_set_y(ui_SleepReportLight, 115);
    sprintf(buf, "浅睡: %dh%02dm", sum.light_min / 60, sum.light_min % 60);
    lv_label_set_text(ui_SleepReportLight, buf);
    lv_obj_set_style_text_font(ui_SleepReportLight, &ui_font_Cuyuan20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_SleepReportLight, lv_color_hex(0x7986CB), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_SleepReportREM = lv_label_create(ui_SleepReportPage);
    lv_obj_set_width(ui_SleepReportREM, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SleepReportREM, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_SleepReportREM, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_x(ui_SleepReportREM, -20);
    lv_obj_set_y(ui_SleepReportREM, 85);
    sprintf(buf, "REM: %dh%02dm", sum.rem_min / 60, sum.rem_min % 60);
    lv_label_set_text(ui_SleepReportREM, buf);
    lv_obj_set_style_text_font(ui_SleepReportREM, &ui_font_Cuyuan20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_SleepReportREM, lv_color_hex(0x9575CD), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_SleepReportAwake = lv_label_create(ui_SleepReportPage);
    lv_obj_set_width(ui_SleepReportAwake, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SleepReportAwake, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_SleepReportAwake, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_x(ui_SleepReportAwake, -20);
    lv_obj_set_y(ui_SleepReportAwake, 115);
    sprintf(buf, "清醒: %dh%02dm", sum.awake_min / 60, sum.awake_min % 60);
    lv_label_set_text(ui_SleepReportAwake, buf);
    lv_obj_set_style_text_font(ui_SleepReportAwake, &ui_font_Cuyuan20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_SleepReportAwake, lv_color_hex(0xEF5350), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_SleepReportHR = lv_label_create(ui_SleepReportPage);
    lv_obj_set_width(ui_SleepReportHR, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SleepReportHR, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_SleepReportHR, LV_ALIGN_TOP_MID);
    lv_obj_set_y(ui_SleepReportHR, 150);
    sprintf(buf, "平均心率 %d  最低 %d  翻身 %d次", sum.avg_hr, sum.min_hr, sum.posture_changes);
    lv_label_set_text(ui_SleepReportHR, buf);
    lv_obj_set_style_text_font(ui_SleepReportHR, &ui_font_Cuyuan20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_SleepReportHR, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Posture distribution */
    lv_obj_t * ui_SleepReportPosture = lv_label_create(ui_SleepReportPage);
    lv_obj_set_width(ui_SleepReportPosture, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SleepReportPosture, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_SleepReportPosture, LV_ALIGN_TOP_MID);
    lv_obj_set_y(ui_SleepReportPosture, 180);
    sprintf(buf, "仰%d' 左%d' 右%d' 俯%d'",
            sum.supine_min, sum.left_min, sum.right_min, sum.prone_min);
    lv_label_set_text(ui_SleepReportPosture, buf);
    lv_obj_set_style_text_font(ui_SleepReportPosture, &ui_font_Cuyuan20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_SleepReportPosture, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Back button */
    ui_SleepReportBackBtn = lv_btn_create(ui_SleepReportPage);
    lv_obj_set_width(ui_SleepReportBackBtn, 120);
    lv_obj_set_height(ui_SleepReportBackBtn, 40);
    lv_obj_set_align(ui_SleepReportBackBtn, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(ui_SleepReportBackBtn, -15);
    lv_obj_set_style_radius(ui_SleepReportBackBtn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_SleepReportBackBtn, lv_color_hex(0x5C6BC0), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * btn_label = lv_label_create(ui_SleepReportBackBtn);
    lv_obj_set_width(btn_label, LV_SIZE_CONTENT);
    lv_obj_set_height(btn_label, LV_SIZE_CONTENT);
    lv_obj_set_align(btn_label, LV_ALIGN_CENTER);
    lv_label_set_text(btn_label, "返回");
    lv_obj_set_style_text_font(btn_label, &ui_font_Cuyuan20, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_SleepReportPage, ui_event_SleepReportPage, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_SleepReportBackBtn, ui_event_SleepReportBackBtn, LV_EVENT_ALL, NULL);
}

void ui_SleepReportPage_screen_deinit(void)
{
}
