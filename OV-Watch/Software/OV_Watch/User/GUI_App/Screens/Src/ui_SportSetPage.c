#include "../../ui.h"
#include "../../ui_helpers.h"
#include "../Inc/ui_SportSetPage.h"
#include "../../../Func/Inc/SportManager.h"

///////////////////// Page Manager //////////////////
Page_t Page_SportSet = {ui_SportSetPage_screen_init, ui_SportSetPage_screen_deinit, &ui_SportSetPage};

///////////////////// VARIABLES ////////////////////
lv_obj_t * ui_SportSetPage;

//list view
static lv_obj_t * ui_SportSetListPanel;
static lv_obj_t * ui_SportSetRowPanel[4];
static lv_obj_t * ui_SportSetRowLabel[4];
static lv_obj_t * ui_SportSetRowValue[4];

//edit view
static lv_obj_t * ui_SportSetEditPanel;
static lv_obj_t * ui_SportSetEditTitle;
static lv_obj_t * ui_SportSetRoller1;
static lv_obj_t * ui_SportSetRoller2;
static lv_obj_t * ui_SportSetOKBtn;
static lv_obj_t * ui_SportSetOKLabel;

static uint8_t ui_SportSetEditMode = 0;   //0:list 1:step goal 2:weight 3:remind time 4:sedentary

static const char * ui_SportSetRowName[4] = {"STEP GOAL", "WEIGHT", "REMIND", "SEDENTARY"};

static const uint16_t step_goal_options[6] = {4000, 6000, 8000, 10000, 12000, 15000};

///////////////////// FUNCTIONS ////////////////////
static void sportset_refresh_values(void)
{
    uint8_t value_strbuf[10];

    sprintf(value_strbuf, "%d", SportSetting.step_goal);
    lv_label_set_text(ui_SportSetRowValue[0], value_strbuf);

    sprintf(value_strbuf, "%dKG", SportSetting.weight_kg);
    lv_label_set_text(ui_SportSetRowValue[1], value_strbuf);

    sprintf(value_strbuf, "%02d:%02d", SportSetting.remind_hour, SportSetting.remind_min);
    lv_label_set_text(ui_SportSetRowValue[2], value_strbuf);

    lv_label_set_text(ui_SportSetRowValue[3], SportSetting.sedentary_en ? "ON" : "OFF");
}

static void sportset_open_editor(uint8_t mode)
{
    uint8_t i;

    ui_SportSetEditMode = mode;
    lv_label_set_text(ui_SportSetEditTitle, ui_SportSetRowName[mode - 1]);
    lv_obj_add_flag(ui_SportSetRoller2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(ui_SportSetRoller1, 0);
    lv_obj_clear_flag(ui_SportSetEditPanel, LV_OBJ_FLAG_HIDDEN);

    switch(mode)
    {
        case 1: //step goal
            lv_roller_set_options(ui_SportSetRoller1, "4000\n6000\n8000\n10000\n12000\n15000", LV_ROLLER_MODE_NORMAL);
            for(i = 0; i < 6; i++)
            {
                if(step_goal_options[i] == SportSetting.step_goal)
                    break;
            }
            lv_roller_set_selected(ui_SportSetRoller1, i < 6 ? i : 2, LV_ANIM_OFF);
            break;
        case 2: //weight
            lv_roller_set_options(ui_SportSetRoller1, "40\n45\n50\n55\n60\n65\n70\n75\n80\n85\n90\n95\n100", LV_ROLLER_MODE_NORMAL);
            if(SportSetting.weight_kg >= 40 && SportSetting.weight_kg <= 100)
                lv_roller_set_selected(ui_SportSetRoller1, (SportSetting.weight_kg - 40) / 5, LV_ANIM_OFF);
            else
                lv_roller_set_selected(ui_SportSetRoller1, 4, LV_ANIM_OFF);
            break;
        case 3: //remind time
            lv_roller_set_options(ui_SportSetRoller1, "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23", LV_ROLLER_MODE_NORMAL);
            lv_roller_set_selected(ui_SportSetRoller1, SportSetting.remind_hour, LV_ANIM_OFF);
            lv_obj_set_x(ui_SportSetRoller1, -50);
            lv_roller_set_options(ui_SportSetRoller2, "00\n15\n30\n45", LV_ROLLER_MODE_NORMAL);
            lv_roller_set_selected(ui_SportSetRoller2, SportSetting.remind_min / 15, LV_ANIM_OFF);
            lv_obj_clear_flag(ui_SportSetRoller2, LV_OBJ_FLAG_HIDDEN);
            break;
        case 4: //sedentary switch
            lv_roller_set_options(ui_SportSetRoller1, "OFF\nON", LV_ROLLER_MODE_NORMAL);
            lv_roller_set_selected(ui_SportSetRoller1, SportSetting.sedentary_en, LV_ANIM_OFF);
            break;
        default:
            break;
    }
}

static void sportset_close_editor(void)
{
    ui_SportSetEditMode = 0;
    lv_obj_add_flag(ui_SportSetEditPanel, LV_OBJ_FLAG_HIDDEN);
    sportset_refresh_values();
}

void ui_event_SportSetPage(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_GESTURE)
    {
        if(lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT)
        {
            if(ui_SportSetEditMode)
                sportset_close_editor();
            else
                Page_Back();
        }
    }
}

void ui_event_SportSetRow(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    uint8_t i;

    if(event_code == LV_EVENT_CLICKED)
    {
        for(i = 0; i < 4; i++)
        {
            if(target == ui_SportSetRowPanel[i])
            {
                sportset_open_editor(i + 1);
                break;
            }
        }
    }
}

void ui_event_SportSetOKBtn(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        switch(ui_SportSetEditMode)
        {
            case 1:
                SportSetting.step_goal = step_goal_options[lv_roller_get_selected(ui_SportSetRoller1)];
                break;
            case 2:
                SportSetting.weight_kg = 40 + 5 * lv_roller_get_selected(ui_SportSetRoller1);
                break;
            case 3:
                SportSetting.remind_hour = lv_roller_get_selected(ui_SportSetRoller1);
                SportSetting.remind_min = 15 * lv_roller_get_selected(ui_SportSetRoller2);
                break;
            case 4:
                SportSetting.sedentary_en = lv_roller_get_selected(ui_SportSetRoller1);
                break;
            default:
                break;
        }
        Sport_SaveSetting();
        sportset_close_editor();
    }
}

///////////////////// SCREEN init ////////////////////
void ui_SportSetPage_screen_init(void)
{
    uint8_t i;

    ui_SportSetPage = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_SportSetPage, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    //list view
    ui_SportSetListPanel = NULL; //rows are directly on the page
    for(i = 0; i < 4; i++)
    {
        ui_SportSetRowPanel[i] = lv_obj_create(ui_SportSetPage);
        lv_obj_set_width(ui_SportSetRowPanel[i], 240);
        lv_obj_set_height(ui_SportSetRowPanel[i], 55);
        lv_obj_set_x(ui_SportSetRowPanel[i], 0);
        lv_obj_set_y(ui_SportSetRowPanel[i], 55 * i);
        lv_obj_set_align(ui_SportSetRowPanel[i], LV_ALIGN_TOP_MID);
        lv_obj_clear_flag(ui_SportSetRowPanel[i], LV_OBJ_FLAG_SCROLLABLE);      /// Flags
        lv_obj_set_style_radius(ui_SportSetRowPanel[i], 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ui_SportSetRowPanel[i], lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(ui_SportSetRowPanel[i], 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(ui_SportSetRowPanel[i], 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ui_SportSetRowPanel[i], lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(ui_SportSetRowPanel[i], 100, LV_PART_MAIN | LV_STATE_PRESSED);

        ui_SportSetRowLabel[i] = lv_label_create(ui_SportSetRowPanel[i]);
        lv_obj_set_width(ui_SportSetRowLabel[i], LV_SIZE_CONTENT);
        lv_obj_set_height(ui_SportSetRowLabel[i], LV_SIZE_CONTENT);
        lv_obj_set_align(ui_SportSetRowLabel[i], LV_ALIGN_LEFT_MID);
        lv_label_set_text(ui_SportSetRowLabel[i], ui_SportSetRowName[i]);
        lv_obj_set_style_text_font(ui_SportSetRowLabel[i], &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

        ui_SportSetRowValue[i] = lv_label_create(ui_SportSetRowPanel[i]);
        lv_obj_set_width(ui_SportSetRowValue[i], LV_SIZE_CONTENT);
        lv_obj_set_height(ui_SportSetRowValue[i], LV_SIZE_CONTENT);
        lv_obj_set_align(ui_SportSetRowValue[i], LV_ALIGN_RIGHT_MID);
        lv_label_set_text(ui_SportSetRowValue[i], "");
        lv_obj_set_style_text_color(ui_SportSetRowValue[i], lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_SportSetRowValue[i], &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_add_event_cb(ui_SportSetRowPanel[i], ui_event_SportSetRow, LV_EVENT_ALL, NULL);
    }
    sportset_refresh_values();

    //edit view
    ui_SportSetEditPanel = lv_obj_create(ui_SportSetPage);
    lv_obj_set_width(ui_SportSetEditPanel, 240);
    lv_obj_set_height(ui_SportSetEditPanel, 280);
    lv_obj_set_align(ui_SportSetEditPanel, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_SportSetEditPanel, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_SportSetEditPanel, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_SportSetEditPanel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_SportSetEditPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_SportSetEditTitle = lv_label_create(ui_SportSetEditPanel);
    lv_obj_set_width(ui_SportSetEditTitle, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportSetEditTitle, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_SportSetEditTitle, 0);
    lv_obj_set_y(ui_SportSetEditTitle, 10);
    lv_obj_set_align(ui_SportSetEditTitle, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_SportSetEditTitle, "");
    lv_obj_set_style_text_font(ui_SportSetEditTitle, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_SportSetRoller1 = lv_roller_create(ui_SportSetEditPanel);
    lv_obj_set_height(ui_SportSetRoller1, 120);
    lv_obj_set_width(ui_SportSetRoller1, 90);
    lv_obj_set_x(ui_SportSetRoller1, -50);
    lv_obj_set_y(ui_SportSetRoller1, 0);
    lv_obj_set_align(ui_SportSetRoller1, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(ui_SportSetRoller1, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_SportSetRoller1, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_SportSetRoller1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_SportSetRoller1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_SportSetRoller1, lv_color_hex(0xFFFFFF), LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_SportSetRoller1, 0, LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_color(ui_SportSetRoller1, lv_color_hex(0x0064FF), LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(ui_SportSetRoller1, 2, LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_pad(ui_SportSetRoller1, 1, LV_PART_SELECTED | LV_STATE_DEFAULT);

    ui_SportSetRoller2 = lv_roller_create(ui_SportSetEditPanel);
    lv_obj_set_height(ui_SportSetRoller2, 120);
    lv_obj_set_width(ui_SportSetRoller2, 90);
    lv_obj_set_x(ui_SportSetRoller2, 50);
    lv_obj_set_y(ui_SportSetRoller2, 0);
    lv_obj_set_align(ui_SportSetRoller2, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(ui_SportSetRoller2, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_SportSetRoller2, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_SportSetRoller2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_SportSetRoller2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_SportSetRoller2, lv_color_hex(0xFFFFFF), LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_SportSetRoller2, 0, LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_color(ui_SportSetRoller2, lv_color_hex(0x0064FF), LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(ui_SportSetRoller2, 2, LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_pad(ui_SportSetRoller2, 1, LV_PART_SELECTED | LV_STATE_DEFAULT);

    ui_SportSetOKBtn = lv_btn_create(ui_SportSetEditPanel);
    lv_obj_set_width(ui_SportSetOKBtn, 100);
    lv_obj_set_height(ui_SportSetOKBtn, 40);
    lv_obj_set_x(ui_SportSetOKBtn, 0);
    lv_obj_set_y(ui_SportSetOKBtn, -15);
    lv_obj_set_align(ui_SportSetOKBtn, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_style_radius(ui_SportSetOKBtn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_SportSetOKBtn, lv_color_hex(0x5FB878), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_SportSetOKLabel = lv_label_create(ui_SportSetOKBtn);
    lv_obj_set_width(ui_SportSetOKLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_SportSetOKLabel, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_SportSetOKLabel, LV_ALIGN_CENTER);
    lv_label_set_text(ui_SportSetOKLabel, "OK");
    lv_obj_set_style_text_font(ui_SportSetOKLabel, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_flag(ui_SportSetEditPanel, LV_OBJ_FLAG_HIDDEN);

    ui_SportSetEditMode = 0;

    //events
    lv_obj_add_event_cb(ui_SportSetPage, ui_event_SportSetPage, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_SportSetOKBtn, ui_event_SportSetOKBtn, LV_EVENT_ALL, NULL);
}

/////////////////// SCREEN deinit ////////////////////
void ui_SportSetPage_screen_deinit(void)
{}
