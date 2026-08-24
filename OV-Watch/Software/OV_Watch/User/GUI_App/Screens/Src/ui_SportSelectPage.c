#include "../../ui.h"
#include "../../ui_helpers.h"
#include "../Inc/ui_SportSelectPage.h"
#include "../Inc/ui_SportPage.h"
#include "../../../Func/Inc/SportManager.h"

///////////////////// Page Manager //////////////////
Page_t Page_SportSelect = {ui_SportSelectPage_screen_init, ui_SportSelectPage_screen_deinit, &ui_SportSelectPage};

///////////////////// VARIABLES ////////////////////
lv_obj_t * ui_SportSelectPage;
lv_obj_t * ui_SportSelTitleLabel;

lv_obj_t * ui_SportSelRunPanel;
lv_obj_t * ui_SportSelRunButton;
lv_obj_t * ui_SportSelRunicon;
lv_obj_t * ui_SportSelRunLabel;

lv_obj_t * ui_SportSelWalkPanel;
lv_obj_t * ui_SportSelWalkButton;
lv_obj_t * ui_SportSelWalkicon;
lv_obj_t * ui_SportSelWalkLabel;

lv_obj_t * ui_SportSelRidePanel;
lv_obj_t * ui_SportSelRideButton;
lv_obj_t * ui_SportSelRideicon;
lv_obj_t * ui_SportSelRideLabel;

lv_obj_t * ui_SportSelNoticeLabel;

///////////////////// FUNCTIONS ////////////////////
void ui_event_SportSelectPage(lv_event_t * e)
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

static void sport_entry_click(SportType_t type)
{
    // start a new session, or back to the running one
    if(SportNow.state == SPORT_IDLE)
    {
        Sport_Start(type);
        Page_Load(&Page_Sport);
    }
    else if(SportNow.type == type)
    {
        Page_Load(&Page_Sport);
    }
}

void ui_event_SportSelRunPanel(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        sport_entry_click(SPORT_RUN);
    }
}

void ui_event_SportSelWalkPanel(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        sport_entry_click(SPORT_WALK);
    }
}

void ui_event_SportSelRidePanel(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        sport_entry_click(SPORT_RIDE);
    }
}

///////////////////// SCREEN init ////////////////////
void ui_SportSelectPage_screen_init(void)
{
    ui_SportSelectPage = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_SportSelectPage, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    ui_SportSelTitleLabel = lv_label_create(ui_SportSelectPage);
    lv_obj_set_width(ui_SportSelTitleLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportSelTitleLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_SportSelTitleLabel, 0);
    lv_obj_set_y(ui_SportSelTitleLabel, 15);
    lv_obj_set_align(ui_SportSelTitleLabel, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_SportSelTitleLabel, "运 动");
    lv_obj_set_style_text_font(ui_SportSelTitleLabel, &ui_font_Cuyuan20, LV_PART_MAIN | LV_STATE_DEFAULT);

    // RUN
    ui_SportSelRunPanel = lv_obj_create(ui_SportSelectPage);
    lv_obj_set_width(ui_SportSelRunPanel, 240);
    lv_obj_set_height(ui_SportSelRunPanel, 60);
    lv_obj_set_x(ui_SportSelRunPanel, 0);
    lv_obj_set_y(ui_SportSelRunPanel, 55);
    lv_obj_set_align(ui_SportSelRunPanel, LV_ALIGN_TOP_MID);
    lv_obj_clear_flag(ui_SportSelRunPanel, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(ui_SportSelRunPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_SportSelRunPanel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_SportSelRunPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_SportSelRunPanel, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_SportSelRunPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_SportSelRunPanel, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(ui_SportSelRunPanel, 100, LV_PART_MAIN | LV_STATE_PRESSED);

    ui_SportSelRunButton = lv_btn_create(ui_SportSelRunPanel);
    lv_obj_set_width(ui_SportSelRunButton, 40);
    lv_obj_set_height(ui_SportSelRunButton, 40);
    lv_obj_set_align(ui_SportSelRunButton, LV_ALIGN_LEFT_MID);
    lv_obj_clear_flag(ui_SportSelRunButton, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(ui_SportSelRunButton, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_SportSelRunButton, lv_color_hex(0xFF4040), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_SportSelRunButton, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_SportSelRunicon = lv_label_create(ui_SportSelRunButton);
    lv_obj_set_width(ui_SportSelRunicon, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportSelRunicon, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_SportSelRunicon, LV_ALIGN_CENTER);
    lv_label_set_text(ui_SportSelRunicon, "R");
    lv_obj_set_style_text_font(ui_SportSelRunicon, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_SportSelRunLabel = lv_label_create(ui_SportSelRunPanel);
    lv_obj_set_width(ui_SportSelRunLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportSelRunLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_SportSelRunLabel, 60);
    lv_obj_set_y(ui_SportSelRunLabel, 0);
    lv_obj_set_align(ui_SportSelRunLabel, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_SportSelRunLabel, "RUN");
    lv_obj_set_style_text_font(ui_SportSelRunLabel, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    // WALK
    ui_SportSelWalkPanel = lv_obj_create(ui_SportSelectPage);
    lv_obj_set_width(ui_SportSelWalkPanel, 240);
    lv_obj_set_height(ui_SportSelWalkPanel, 60);
    lv_obj_set_x(ui_SportSelWalkPanel, 0);
    lv_obj_set_y(ui_SportSelWalkPanel, 125);
    lv_obj_set_align(ui_SportSelWalkPanel, LV_ALIGN_TOP_MID);
    lv_obj_clear_flag(ui_SportSelWalkPanel, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(ui_SportSelWalkPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_SportSelWalkPanel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_SportSelWalkPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_SportSelWalkPanel, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_SportSelWalkPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_SportSelWalkPanel, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(ui_SportSelWalkPanel, 100, LV_PART_MAIN | LV_STATE_PRESSED);

    ui_SportSelWalkButton = lv_btn_create(ui_SportSelWalkPanel);
    lv_obj_set_width(ui_SportSelWalkButton, 40);
    lv_obj_set_height(ui_SportSelWalkButton, 40);
    lv_obj_set_align(ui_SportSelWalkButton, LV_ALIGN_LEFT_MID);
    lv_obj_clear_flag(ui_SportSelWalkButton, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(ui_SportSelWalkButton, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_SportSelWalkButton, lv_color_hex(0x40C040), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_SportSelWalkButton, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_SportSelWalkicon = lv_label_create(ui_SportSelWalkButton);
    lv_obj_set_width(ui_SportSelWalkicon, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportSelWalkicon, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_SportSelWalkicon, LV_ALIGN_CENTER);
    lv_label_set_text(ui_SportSelWalkicon, "W");
    lv_obj_set_style_text_font(ui_SportSelWalkicon, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_SportSelWalkLabel = lv_label_create(ui_SportSelWalkPanel);
    lv_obj_set_width(ui_SportSelWalkLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportSelWalkLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_SportSelWalkLabel, 60);
    lv_obj_set_y(ui_SportSelWalkLabel, 0);
    lv_obj_set_align(ui_SportSelWalkLabel, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_SportSelWalkLabel, "WALK");
    lv_obj_set_style_text_font(ui_SportSelWalkLabel, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    // RIDE
    ui_SportSelRidePanel = lv_obj_create(ui_SportSelectPage);
    lv_obj_set_width(ui_SportSelRidePanel, 240);
    lv_obj_set_height(ui_SportSelRidePanel, 60);
    lv_obj_set_x(ui_SportSelRidePanel, 0);
    lv_obj_set_y(ui_SportSelRidePanel, 195);
    lv_obj_set_align(ui_SportSelRidePanel, LV_ALIGN_TOP_MID);
    lv_obj_clear_flag(ui_SportSelRidePanel, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(ui_SportSelRidePanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_SportSelRidePanel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_SportSelRidePanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_SportSelRidePanel, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_SportSelRidePanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_SportSelRidePanel, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(ui_SportSelRidePanel, 100, LV_PART_MAIN | LV_STATE_PRESSED);

    ui_SportSelRideButton = lv_btn_create(ui_SportSelRidePanel);
    lv_obj_set_width(ui_SportSelRideButton, 40);
    lv_obj_set_height(ui_SportSelRideButton, 40);
    lv_obj_set_align(ui_SportSelRideButton, LV_ALIGN_LEFT_MID);
    lv_obj_clear_flag(ui_SportSelRideButton, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(ui_SportSelRideButton, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_SportSelRideButton, lv_color_hex(0x4080FF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_SportSelRideButton, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_SportSelRideicon = lv_label_create(ui_SportSelRideButton);
    lv_obj_set_width(ui_SportSelRideicon, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportSelRideicon, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_SportSelRideicon, LV_ALIGN_CENTER);
    lv_label_set_text(ui_SportSelRideicon, "C");
    lv_obj_set_style_text_font(ui_SportSelRideicon, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_SportSelRideLabel = lv_label_create(ui_SportSelRidePanel);
    lv_obj_set_width(ui_SportSelRideLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportSelRideLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_SportSelRideLabel, 60);
    lv_obj_set_y(ui_SportSelRideLabel, 0);
    lv_obj_set_align(ui_SportSelRideLabel, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_SportSelRideLabel, "RIDE");
    lv_obj_set_style_text_font(ui_SportSelRideLabel, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    // recording notice
    ui_SportSelNoticeLabel = lv_label_create(ui_SportSelectPage);
    lv_obj_set_width(ui_SportSelNoticeLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportSelNoticeLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_SportSelNoticeLabel, 0);
    lv_obj_set_y(ui_SportSelNoticeLabel, -5);
    lv_obj_set_align(ui_SportSelNoticeLabel, LV_ALIGN_BOTTOM_MID);
    if(SportNow.state != SPORT_IDLE)
    {
        lv_label_set_text(ui_SportSelNoticeLabel, "REC...");
    }
    else
    {
        lv_label_set_text(ui_SportSelNoticeLabel, "");
    }
    lv_obj_set_style_text_color(ui_SportSelNoticeLabel, lv_color_hex(0xFF4040), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_SportSelNoticeLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    //events
    lv_obj_add_event_cb(ui_SportSelectPage, ui_event_SportSelectPage, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_SportSelRunPanel, ui_event_SportSelRunPanel, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_SportSelWalkPanel, ui_event_SportSelWalkPanel, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_SportSelRidePanel, ui_event_SportSelRidePanel, LV_EVENT_ALL, NULL);
}

/////////////////// SCREEN deinit ////////////////////
void ui_SportSelectPage_screen_deinit(void)
{}
