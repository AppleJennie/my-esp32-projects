#include "../../ui.h"
#include "../../ui_helpers.h"
#include "../Inc/ui_SportPage.h"
#include "../../../Func/Inc/SportManager.h"

///////////////////// Page Manager //////////////////
Page_t Page_Sport = {ui_SportPage_screen_init, ui_SportPage_screen_deinit, &ui_SportPage};

///////////////////// VARIABLES ////////////////////
lv_obj_t * ui_SportPage;
lv_obj_t * ui_SportTypeLabel;
lv_obj_t * ui_SportTimeLabel;
lv_obj_t * ui_SportHRTitleLabel;
lv_obj_t * ui_SportHRValueLabel;
lv_obj_t * ui_SportHRZoneLabel;
lv_obj_t * ui_SportStepTitleLabel;
lv_obj_t * ui_SportStepValueLabel;
lv_obj_t * ui_SportKcalTitleLabel;
lv_obj_t * ui_SportKcalValueLabel;
lv_obj_t * ui_SportPauseBtn;
lv_obj_t * ui_SportPauseLabel;
lv_obj_t * ui_SportStopBtn;
lv_obj_t * ui_SportStopLabel;
lv_obj_t * ui_SportOkBtn;
lv_obj_t * ui_SportOkLabel;

lv_timer_t * ui_SportPageTimer;

static const char * sport_type_str[SPORT_TYPE_NUM] = {"RUN", "WALK", "RIDE"};
static const char * sport_zone_str[4] = {"WARM UP", "FAT BURN", "CARDIO", "PEAK"};

/////////////////// private timer ///////////////////
// need to be destroyed when the page is destroyed
static void SportPage_timer_cb(lv_timer_t * timer)
{
    uint8_t value_strbuf[12];
    uint32_t sec = SportNow.duration_s;

    // duration
    if(sec < 3600)
    {
        sprintf(value_strbuf, "%02d:%02d", sec / 60, sec % 60);
    }
    else
    {
        sprintf(value_strbuf, "%d:%02d:%02d", sec / 3600, sec / 60 % 60, sec % 60);
    }
    lv_label_set_text(ui_SportTimeLabel, value_strbuf);

    // heart rate and zone
    sprintf(value_strbuf, "%d", SportNow.hr);
    lv_label_set_text(ui_SportHRValueLabel, value_strbuf);
    lv_label_set_text(ui_SportHRZoneLabel, sport_zone_str[Sport_HRZone(SportNow.hr)]);

    // steps and kcal
    sprintf(value_strbuf, "%d", SportNow.steps);
    lv_label_set_text(ui_SportStepValueLabel, value_strbuf);
    sprintf(value_strbuf, "%d", SportNow.kcal);
    lv_label_set_text(ui_SportKcalValueLabel, value_strbuf);
}

///////////////////// FUNCTIONS ////////////////////
void ui_event_SportPage(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_GESTURE)
    {
        if(lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT)
        {
            // session keeps recording in the background
            Page_Back();
        }
    }
}

void ui_event_SportPauseBtn(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        if(SportNow.state == SPORT_RUNNING)
        {
            Sport_Pause();
            lv_label_set_text(ui_SportPauseLabel, "GO");
        }
        else if(SportNow.state == SPORT_PAUSED)
        {
            Sport_Resume();
            lv_label_set_text(ui_SportPauseLabel, "PAUSE");
        }
    }
}

void ui_event_SportStopBtn(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        Sport_Stop();
        // show the summary: final values stay, only OK button
        lv_label_set_text(ui_SportTypeLabel, "END");
        lv_obj_add_flag(ui_SportPauseBtn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_SportStopBtn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_SportOkBtn, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_event_SportOkBtn(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        Page_Back();
    }
}

///////////////////// SCREEN init ////////////////////
void ui_SportPage_screen_init(void)
{
    ui_SportPage = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_SportPage, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    // sport type
    ui_SportTypeLabel = lv_label_create(ui_SportPage);
    lv_obj_set_width(ui_SportTypeLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportTypeLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_SportTypeLabel, 0);
    lv_obj_set_y(ui_SportTypeLabel, 10);
    lv_obj_set_align(ui_SportTypeLabel, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_SportTypeLabel, sport_type_str[SportNow.type]);
    lv_obj_set_style_text_font(ui_SportTypeLabel, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    // duration
    ui_SportTimeLabel = lv_label_create(ui_SportPage);
    lv_obj_set_width(ui_SportTimeLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportTimeLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_SportTimeLabel, 0);
    lv_obj_set_y(ui_SportTimeLabel, 38);
    lv_obj_set_align(ui_SportTimeLabel, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_SportTimeLabel, "00:00");
    lv_obj_set_style_text_font(ui_SportTimeLabel, &ui_font_Cuyuan38, LV_PART_MAIN | LV_STATE_DEFAULT);

    // heart rate
    ui_SportHRTitleLabel = lv_label_create(ui_SportPage);
    lv_obj_set_width(ui_SportHRTitleLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportHRTitleLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_SportHRTitleLabel, -50);
    lv_obj_set_y(ui_SportHRTitleLabel, 92);
    lv_obj_set_align(ui_SportHRTitleLabel, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_SportHRTitleLabel, "心 率");
    lv_obj_set_style_text_color(ui_SportHRTitleLabel, lv_color_hex(0xFF4040), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_SportHRTitleLabel, &ui_font_Cuyuan20, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_SportHRValueLabel = lv_label_create(ui_SportPage);
    lv_obj_set_width(ui_SportHRValueLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportHRValueLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_SportHRValueLabel, 45);
    lv_obj_set_y(ui_SportHRValueLabel, 88);
    lv_obj_set_align(ui_SportHRValueLabel, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_SportHRValueLabel, "0");
    lv_obj_set_style_text_color(ui_SportHRValueLabel, lv_color_hex(0xFF4040), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_SportHRValueLabel, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_SportHRZoneLabel = lv_label_create(ui_SportPage);
    lv_obj_set_width(ui_SportHRZoneLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportHRZoneLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_SportHRZoneLabel, 0);
    lv_obj_set_y(ui_SportHRZoneLabel, 122);
    lv_obj_set_align(ui_SportHRZoneLabel, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_SportHRZoneLabel, "WARM UP");
    lv_obj_set_style_text_color(ui_SportHRZoneLabel, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_SportHRZoneLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    // steps
    ui_SportStepTitleLabel = lv_label_create(ui_SportPage);
    lv_obj_set_width(ui_SportStepTitleLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportStepTitleLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_SportStepTitleLabel, -50);
    lv_obj_set_y(ui_SportStepTitleLabel, 148);
    lv_obj_set_align(ui_SportStepTitleLabel, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_SportStepTitleLabel, "步数");
    lv_obj_set_style_text_font(ui_SportStepTitleLabel, &ui_font_Cuyuan18, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_SportStepValueLabel = lv_label_create(ui_SportPage);
    lv_obj_set_width(ui_SportStepValueLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportStepValueLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_SportStepValueLabel, 45);
    lv_obj_set_y(ui_SportStepValueLabel, 146);
    lv_obj_set_align(ui_SportStepValueLabel, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_SportStepValueLabel, "0");
    lv_obj_set_style_text_font(ui_SportStepValueLabel, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    // kcal
    ui_SportKcalTitleLabel = lv_label_create(ui_SportPage);
    lv_obj_set_width(ui_SportKcalTitleLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportKcalTitleLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_SportKcalTitleLabel, -50);
    lv_obj_set_y(ui_SportKcalTitleLabel, 178);
    lv_obj_set_align(ui_SportKcalTitleLabel, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_SportKcalTitleLabel, "KCAL");
    lv_obj_set_style_text_color(ui_SportKcalTitleLabel, lv_color_hex(0xFFC000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_SportKcalTitleLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_SportKcalValueLabel = lv_label_create(ui_SportPage);
    lv_obj_set_width(ui_SportKcalValueLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportKcalValueLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_SportKcalValueLabel, 45);
    lv_obj_set_y(ui_SportKcalValueLabel, 174);
    lv_obj_set_align(ui_SportKcalValueLabel, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_SportKcalValueLabel, "0");
    lv_obj_set_style_text_color(ui_SportKcalValueLabel, lv_color_hex(0xFFC000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_SportKcalValueLabel, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    // pause button
    ui_SportPauseBtn = lv_btn_create(ui_SportPage);
    lv_obj_set_width(ui_SportPauseBtn, 100);
    lv_obj_set_height(ui_SportPauseBtn, 40);
    lv_obj_set_x(ui_SportPauseBtn, -55);
    lv_obj_set_y(ui_SportPauseBtn, -12);
    lv_obj_set_align(ui_SportPauseBtn, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_style_radius(ui_SportPauseBtn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_SportPauseBtn, lv_color_hex(0x009680), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_SportPauseLabel = lv_label_create(ui_SportPauseBtn);
    lv_obj_set_width(ui_SportPauseLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportPauseLabel, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_SportPauseLabel, LV_ALIGN_CENTER);
    lv_label_set_text(ui_SportPauseLabel, "PAUSE");
    lv_obj_set_style_text_font(ui_SportPauseLabel, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    // stop button
    ui_SportStopBtn = lv_btn_create(ui_SportPage);
    lv_obj_set_width(ui_SportStopBtn, 100);
    lv_obj_set_height(ui_SportStopBtn, 40);
    lv_obj_set_x(ui_SportStopBtn, 55);
    lv_obj_set_y(ui_SportStopBtn, -12);
    lv_obj_set_align(ui_SportStopBtn, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_style_radius(ui_SportStopBtn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_SportStopBtn, lv_color_hex(0xC80000), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_SportStopLabel = lv_label_create(ui_SportStopBtn);
    lv_obj_set_width(ui_SportStopLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportStopLabel, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_SportStopLabel, LV_ALIGN_CENTER);
    lv_label_set_text(ui_SportStopLabel, "STOP");
    lv_obj_set_style_text_font(ui_SportStopLabel, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ok button, only shows after stop
    ui_SportOkBtn = lv_btn_create(ui_SportPage);
    lv_obj_set_width(ui_SportOkBtn, 100);
    lv_obj_set_height(ui_SportOkBtn, 40);
    lv_obj_set_x(ui_SportOkBtn, 0);
    lv_obj_set_y(ui_SportOkBtn, -12);
    lv_obj_set_align(ui_SportOkBtn, LV_ALIGN_BOTTOM_MID);
    lv_obj_add_flag(ui_SportOkBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_radius(ui_SportOkBtn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_SportOkBtn, lv_color_hex(0x5FB878), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_SportOkLabel = lv_label_create(ui_SportOkBtn);
    lv_obj_set_width(ui_SportOkLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportOkLabel, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_SportOkLabel, LV_ALIGN_CENTER);
    lv_label_set_text(ui_SportOkLabel, "OK");
    lv_obj_set_style_text_font(ui_SportOkLabel, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    //when back to a paused session, show the right button text
    if(SportNow.state == SPORT_PAUSED)
    {
        lv_label_set_text(ui_SportPauseLabel, "GO");
    }

    //events
    lv_obj_add_event_cb(ui_SportPage, ui_event_SportPage, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_SportPauseBtn, ui_event_SportPauseBtn, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_SportStopBtn, ui_event_SportStopBtn, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_SportOkBtn, ui_event_SportOkBtn, LV_EVENT_ALL, NULL);

    //timer
    ui_SportPageTimer = lv_timer_create(SportPage_timer_cb, 500, NULL);
}

/////////////////// SCREEN deinit ////////////////////
void ui_SportPage_screen_deinit(void)
{
    lv_timer_del(ui_SportPageTimer);
}
