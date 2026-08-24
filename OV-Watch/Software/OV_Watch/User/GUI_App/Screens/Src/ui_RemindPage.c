#include "../../ui.h"
#include "../../ui_helpers.h"
#include "../Inc/ui_RemindPage.h"
#include "../../../Func/Inc/HWDataAccess.h"
#include "../../../Func/Inc/SportManager.h"

///////////////////// Page Manager //////////////////
Page_t Page_Remind = {ui_RemindPage_screen_init, ui_RemindPage_screen_deinit, &ui_RemindPage};

///////////////////// VARIABLES ////////////////////
lv_obj_t * ui_RemindPage;
lv_obj_t * ui_RemindTitleLabel;
lv_obj_t * ui_RemindCnLabel;
lv_obj_t * ui_RemindValueLabel;
lv_obj_t * ui_RemindOkBtn;
lv_obj_t * ui_RemindOkLabel;

uint8_t ui_RemindType = REMIND_SEDENTARY;

///////////////////// FUNCTIONS ////////////////////
void ui_event_RemindOkBtn(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        Page_Back();
    }
}

void ui_event_RemindPage(lv_event_t * e)
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
void ui_RemindPage_screen_init(void)
{
    uint8_t value_strbuf[20];

    ui_RemindPage = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_RemindPage, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    ui_RemindTitleLabel = lv_label_create(ui_RemindPage);
    lv_obj_set_width(ui_RemindTitleLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_RemindTitleLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_RemindTitleLabel, 0);
    lv_obj_set_y(ui_RemindTitleLabel, 40);
    lv_obj_set_align(ui_RemindTitleLabel, LV_ALIGN_TOP_MID);
    lv_obj_set_style_text_color(ui_RemindTitleLabel, lv_color_hex(0xFF8040), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_RemindTitleLabel, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_RemindCnLabel = lv_label_create(ui_RemindPage);
    lv_obj_set_width(ui_RemindCnLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_RemindCnLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_RemindCnLabel, 0);
    lv_obj_set_y(ui_RemindCnLabel, 90);
    lv_obj_set_align(ui_RemindCnLabel, LV_ALIGN_TOP_MID);
    lv_obj_set_style_text_font(ui_RemindCnLabel, &ui_font_Cuyuan18, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_RemindValueLabel = lv_label_create(ui_RemindPage);
    lv_obj_set_width(ui_RemindValueLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_RemindValueLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_RemindValueLabel, 0);
    lv_obj_set_y(ui_RemindValueLabel, 120);
    lv_obj_set_align(ui_RemindValueLabel, LV_ALIGN_TOP_MID);
    lv_obj_set_style_text_font(ui_RemindValueLabel, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    if(ui_RemindType == REMIND_SEDENTARY)
    {
        lv_label_set_text(ui_RemindTitleLabel, "TIME TO MOVE!");
        lv_label_set_text(ui_RemindCnLabel, "今日步数");
        sprintf(value_strbuf, "%d/%d", HWInterface.IMU.Steps, SportSetting.step_goal);
        lv_label_set_text(ui_RemindValueLabel, value_strbuf);
    }
    else
    {
        lv_label_set_text(ui_RemindTitleLabel, "SPORT TIME!");
        lv_label_set_text(ui_RemindCnLabel, "今日步数");
        sprintf(value_strbuf, "%d/%dMIN", SportTodayMin, SportSetting.sport_goal_min);
        lv_label_set_text(ui_RemindValueLabel, value_strbuf);
    }

    ui_RemindOkBtn = lv_btn_create(ui_RemindPage);
    lv_obj_set_width(ui_RemindOkBtn, 100);
    lv_obj_set_height(ui_RemindOkBtn, 40);
    lv_obj_set_x(ui_RemindOkBtn, 0);
    lv_obj_set_y(ui_RemindOkBtn, -20);
    lv_obj_set_align(ui_RemindOkBtn, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_style_radius(ui_RemindOkBtn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_RemindOkBtn, lv_color_hex(0x5FB878), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_RemindOkLabel = lv_label_create(ui_RemindOkBtn);
    lv_obj_set_width(ui_RemindOkLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_RemindOkLabel, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_RemindOkLabel, LV_ALIGN_CENTER);
    lv_label_set_text(ui_RemindOkLabel, "OK");
    lv_obj_set_style_text_font(ui_RemindOkLabel, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    //events
    lv_obj_add_event_cb(ui_RemindPage, ui_event_RemindPage, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_RemindOkBtn, ui_event_RemindOkBtn, LV_EVENT_ALL, NULL);
}

/////////////////// SCREEN deinit ////////////////////
void ui_RemindPage_screen_deinit(void)
{}
