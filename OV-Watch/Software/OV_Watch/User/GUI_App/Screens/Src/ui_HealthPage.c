#include "../../ui.h"
#include "../../ui_helpers.h"
#include "../Inc/ui_HealthPage.h"
#include "../../../Func/Inc/HWDataAccess.h"
#include "../../../Func/Inc/SportManager.h"

///////////////////// Page Manager //////////////////
Page_t Page_Health = {ui_HealthPage_screen_init, ui_HealthPage_screen_deinit, &ui_HealthPage};

///////////////////// VARIABLES ////////////////////
lv_obj_t * ui_HealthPage;
lv_obj_t * ui_HealthTitleLabel;
lv_obj_t * ui_HealthStepLabel;
lv_obj_t * ui_HealthKcalLabel;
lv_obj_t * ui_HealthSportLabel;
lv_obj_t * ui_HealthHRLabel;
lv_obj_t * ui_HealthHisTitleLabel;
lv_obj_t * ui_HealthHisLabel[SPORT_RECORD_NUM];

static const char * health_sport_str[SPORT_TYPE_NUM] = {"RUN", "WALK", "RIDE"};

///////////////////// FUNCTIONS ////////////////////
void ui_event_HealthPage(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_GESTURE)
    {
        if(lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT)
        {
            Page_Back();
        }
    }
}

///////////////////// SCREEN init ////////////////////
void ui_HealthPage_screen_init(void)
{
    uint8_t value_strbuf[40];
    uint8_t i;

    ui_HealthPage = lv_obj_create(NULL);

    ui_HealthTitleLabel = lv_label_create(ui_HealthPage);
    lv_obj_set_width(ui_HealthTitleLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_HealthTitleLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_HealthTitleLabel, 0);
    lv_obj_set_y(ui_HealthTitleLabel, 10);
    lv_obj_set_align(ui_HealthTitleLabel, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_HealthTitleLabel, "TODAY");
    lv_obj_set_style_text_font(ui_HealthTitleLabel, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    //today steps
    ui_HealthStepLabel = lv_label_create(ui_HealthPage);
    lv_obj_set_width(ui_HealthStepLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_HealthStepLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_HealthStepLabel, 0);
    lv_obj_set_y(ui_HealthStepLabel, 45);
    lv_obj_set_align(ui_HealthStepLabel, LV_ALIGN_TOP_MID);
    sprintf(value_strbuf, "STEP %d/%d", HWInterface.IMU.Steps, SportSetting.step_goal);
    lv_label_set_text(ui_HealthStepLabel, value_strbuf);
    lv_obj_set_style_text_font(ui_HealthStepLabel, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    //today kcal
    ui_HealthKcalLabel = lv_label_create(ui_HealthPage);
    lv_obj_set_width(ui_HealthKcalLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_HealthKcalLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_HealthKcalLabel, 0);
    lv_obj_set_y(ui_HealthKcalLabel, 75);
    lv_obj_set_align(ui_HealthKcalLabel, LV_ALIGN_TOP_MID);
    sprintf(value_strbuf, "KCAL %d", SportTodayKcal);
    lv_label_set_text(ui_HealthKcalLabel, value_strbuf);
    lv_obj_set_style_text_color(ui_HealthKcalLabel, lv_color_hex(0xFFC000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_HealthKcalLabel, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    //today sport minutes
    ui_HealthSportLabel = lv_label_create(ui_HealthPage);
    lv_obj_set_width(ui_HealthSportLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_HealthSportLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_HealthSportLabel, 0);
    lv_obj_set_y(ui_HealthSportLabel, 105);
    lv_obj_set_align(ui_HealthSportLabel, LV_ALIGN_TOP_MID);
    sprintf(value_strbuf, "SPORT %d/%dMIN", SportTodayMin, SportSetting.sport_goal_min);
    lv_label_set_text(ui_HealthSportLabel, value_strbuf);
    lv_obj_set_style_text_color(ui_HealthSportLabel, lv_color_hex(0x009680), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_HealthSportLabel, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    //heart rate
    ui_HealthHRLabel = lv_label_create(ui_HealthPage);
    lv_obj_set_width(ui_HealthHRLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_HealthHRLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_HealthHRLabel, 0);
    lv_obj_set_y(ui_HealthHRLabel, 135);
    lv_obj_set_align(ui_HealthHRLabel, LV_ALIGN_TOP_MID);
    sprintf(value_strbuf, "HR %d", HWInterface.HR_meter.HrRate);
    lv_label_set_text(ui_HealthHRLabel, value_strbuf);
    lv_obj_set_style_text_color(ui_HealthHRLabel, lv_color_hex(0xFF4040), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_HealthHRLabel, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    //history title
    ui_HealthHisTitleLabel = lv_label_create(ui_HealthPage);
    lv_obj_set_width(ui_HealthHisTitleLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_HealthHisTitleLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_HealthHisTitleLabel, 0);
    lv_obj_set_y(ui_HealthHisTitleLabel, 170);
    lv_obj_set_align(ui_HealthHisTitleLabel, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_HealthHisTitleLabel, "HISTORY");
    lv_obj_set_style_text_font(ui_HealthHisTitleLabel, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    //history records
    for(i = 0; i < SPORT_RECORD_NUM; i++)
    {
        SportRecord_t rec;
        if(!Sport_GetRecord(i, &rec))
            break;
        ui_HealthHisLabel[i] = lv_label_create(ui_HealthPage);
        lv_obj_set_width(ui_HealthHisLabel[i], LV_SIZE_CONTENT);
        lv_obj_set_height(ui_HealthHisLabel[i], LV_SIZE_CONTENT);
        lv_obj_set_x(ui_HealthHisLabel[i], 0);
        lv_obj_set_y(ui_HealthHisLabel[i], 200 + 24 * i);
        lv_obj_set_align(ui_HealthHisLabel[i], LV_ALIGN_TOP_MID);
        sprintf(value_strbuf, "%02d-%02d %s %dMIN %dK", rec.month, rec.date,
                health_sport_str[rec.type], rec.duration_min, rec.kcal);
        lv_label_set_text(ui_HealthHisLabel[i], value_strbuf);
        lv_obj_set_style_text_color(ui_HealthHisLabel[i], lv_color_hex(0xAAAAAA), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_HealthHisLabel[i], &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if(i == 0)
    {
        ui_HealthHisLabel[0] = lv_label_create(ui_HealthPage);
        lv_obj_set_width(ui_HealthHisLabel[0], LV_SIZE_CONTENT);
        lv_obj_set_height(ui_HealthHisLabel[0], LV_SIZE_CONTENT);
        lv_obj_set_x(ui_HealthHisLabel[0], 0);
        lv_obj_set_y(ui_HealthHisLabel[0], 200);
        lv_obj_set_align(ui_HealthHisLabel[0], LV_ALIGN_TOP_MID);
        lv_label_set_text(ui_HealthHisLabel[0], "NO DATA");
        lv_obj_set_style_text_color(ui_HealthHisLabel[0], lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_HealthHisLabel[0], &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    //events
    lv_obj_add_event_cb(ui_HealthPage, ui_event_HealthPage, LV_EVENT_ALL, NULL);
}

/////////////////// SCREEN deinit ////////////////////
void ui_HealthPage_screen_deinit(void)
{}
