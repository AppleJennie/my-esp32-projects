/**
 ****************************************************************************************************
 * @file        lvgl_demo.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2023-12-01
 * @brief       LVGL V8移植 实验 — 睡眠监测 UI
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 ESP32-S3 开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#include "lvgl_demo.h"
#include "ltdc.h"
#include "touch.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "sleep_project_config.h"
#include "uart_user.h"
#include "watch_ble_client.h"
#if CONFIG_ENABLE_WIFI
#include "wifi_user.h"
#endif
#if CONFIG_ENABLE_WEATHER
#include "weather.h"
#endif
#if CONFIG_ENABLE_SD_CARD
#include "sd_user.h"
#endif
#if CONFIG_ENABLE_MUSIC_PLAYER
#include "music_user.h"
#endif
#include "sleep_ui.h"
#include "sleep_data.h"
#include "control_panel.h"
#include "hardware_control.h"
#include "sleep_audio_adapter.h"
#include "sleep_monitor_data_adapter.h"

/* UI设计分辨率（适配800x480屏幕） */
#define UI_HOR_RES      800
#define UI_VER_RES      480

static uint16_t g_disp_offset_x = 0;
static uint16_t g_disp_offset_y = 0;

/**
 * @brief       lvgl_demo入口函数
 * @param       无
 * @retval      无
 */
void lvgl_demo(void)
{
    lv_init();              /* 初始化LVGL图形库 */
    lv_port_disp_init();    /* lvgl显示接口初始化,放在lv_init()的后面 */
    lv_port_indev_init();   /* lvgl输入接口初始化,放在lv_init()的后面 */

    /* 为LVGL提供时基单元 */
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, 1 * 1000));

    /* ── 初始化硬件模块 (第一阶段: WiFi/天气/SD/音乐全部关闭) ── */
#if CONFIG_ENABLE_WIFI
    wifi_init();
#endif
    uart_user_init();
#if CONFIG_ENABLE_WEATHER
    time_sync();
#endif
#if CONFIG_ENABLE_SD_CARD
    sd_init();
#endif
#if CONFIG_ENABLE_MUSIC_PLAYER
    music_init();
#endif

    /* 初始化硬件控制模块（FreeRTOS 队列 + 任务） */
    hardware_control_init();

    /* 启动 BLE 手表客户端 */
    watch_ble_client_start();

    /* 初始化睡眠监测UI（主屏幕 + 设置页 + 控制面板） */
    sleep_ui_init();

    /* ★ 初始化音频适配层: INMP441 → audio_pipeline */
#if !CONFIG_SCREEN_ONLY
    sleep_audio_adapter_init();

    /* ★ 初始化雷达+融合数据适配层: R60 → radar_data → SleepData_t */
    sleep_monitor_data_adapter_init();
#endif

    while (1)
    {
        lv_timer_handler();             /* LVGL计时器 */

        /* 每500ms更新：音频+雷达数据 → SleepData_t → UI刷新 */
        static uint32_t last_update = 0;
        uint32_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - last_update >= 500) {
            last_update = now_ms;

            /* ★ 统一数据更新: audio + radar → simplified fusion → SleepData_t */
#if !CONFIG_SCREEN_ONLY
            sleep_monitor_data_adapter_update();
#endif

            /* UI 刷新 */
            sleep_ui_refresh();
            control_panel_refresh();
        }

        vTaskDelay(pdMS_TO_TICKS(10));  /* 延时10毫秒 */
    }
}

/**
 * @brief       初始化并注册显示设备
 * @param       无
 * @retval      无
 */
void lv_port_disp_init(void)
{
    void *buf1 = NULL;
    void *buf2 = NULL;

    /* 初始化显示设备RGBLCD */
    ltdc_init();                /* RGB屏初始化 */
    ltdc_display_dir(1);        /* 设置横屏 */

    /* 全屏显示，无偏移 */
    g_disp_offset_x = 0;
    g_disp_offset_y = 0;

    /*-----------------------------
     * 创建一个绘图缓冲区
     *----------------------------*/
    /* 使用双缓冲 */
    /* 使用 PSRAM 分配缓冲区，避免占用内部 SRAM 导致 WiFi 内存不足 */
    /* 缓冲区高度从 80 改为 40，降低 PSRAM DMA 带宽竞争，减少屏幕闪烁 */
    buf1 = heap_caps_malloc(UI_HOR_RES * 40 * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    buf2 = heap_caps_malloc(UI_HOR_RES * 40 * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);

    /* 初始化显示缓冲区 */
    static lv_disp_draw_buf_t disp_buf;                                 /* 保存显示缓冲区信息的结构体 */
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, UI_HOR_RES * 40);      /* 初始化显示缓冲区 */

    /* 在LVGL中注册显示设备 */
    static lv_disp_drv_t disp_drv;      /* 显示设备的描述符 */
    lv_disp_drv_init(&disp_drv);        /* 初始化显示设备 */

    /* 固定为UI设计分辨率800x480 */
    disp_drv.hor_res = UI_HOR_RES;
    disp_drv.ver_res = UI_VER_RES;

    /* 用来将缓冲区的内容复制到显示设备 */
    disp_drv.flush_cb = lvgl_disp_flush_cb;

    /* 设置显示缓冲区 */
    disp_drv.draw_buf = &disp_buf;

    disp_drv.user_data = panel_handle;

    /* 注册显示设备 */
    lv_disp_drv_register(&disp_drv);
}

/**
 * @brief       初始化并注册输入设备
 * @param       无
 * @retval      无
 */
void lv_port_indev_init(void)
{
    /* 初始化触摸屏 */
    tp_dev.init();

    /* 初始化输入设备 */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);

    /* 配置输入设备类型 */
    indev_drv.type = LV_INDEV_TYPE_POINTER;

    /* 设置输入设备读取回调函数 */
    indev_drv.read_cb = touchpad_read;

    /* 在LVGL中注册驱动程序 */
    lv_indev_drv_register(&indev_drv);
}

/**
* @brief    将内部缓冲区的内容刷新到显示屏上的特定区域
* @note     800x480 UI 全屏显示
* @param    drv : 显示设备
* @param    area : 要刷新的区域
* @param    color_map : 颜色数组
* @retval   无
*/
static void lvgl_disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;

    /* 直接刷新，无偏移 */
    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2 + 1;
    int y2 = area->y2 + 1;

    /* 特定区域打点 */
    esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2, y2, color_map);

    /* 重要!!! 通知图形库，已经刷新完毕了 */
    lv_disp_flush_ready(drv);
}

/**
 * @brief       告诉LVGL运行时间
 * @param       arg : 传入参数(未用到)
 * @retval      无
 */
static void increase_lvgl_tick(void *arg)
{
    /* 告诉LVGL已经过了多少毫秒 */
    lv_tick_inc(1);
}

/**
 * @brief       获取触摸屏设备的状态
 * @param       无
 * @retval      返回触摸屏设备是否被按下
 */
static bool touchpad_is_pressed(void)
{
    tp_dev.scan(0);     /* 触摸按键扫描 */

    if (tp_dev.sta & TP_PRES_DOWN)
    {
        return true;
    }

    return false;
}


/**
 * @brief       在触摸屏被按下的时候读取 x、y 坐标
 * @param       x   : x坐标的指针
 * @param       y   : y坐标的指针
 * @retval      无
 */
static void touchpad_get_xy(lv_coord_t *x, lv_coord_t *y)
{
    (*x) = tp_dev.x[0];
    (*y) = tp_dev.y[0];
}

/**
 * @brief       图形库的触摸屏读取回调函数
 * @param       indev_drv   : 触摸屏设备
 * @param       data        : 输入设备数据结构体
 * @retval      无
 */
void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;

    /* 保存按下的坐标和状态 */
    if(touchpad_is_pressed())
    {
        touchpad_get_xy(&last_x, &last_y);  /* 在触摸屏被按下的时候读取 x、y 坐标 */
        data->state = LV_INDEV_STATE_PR;
    }
    else
    {
        data->state = LV_INDEV_STATE_REL;
    }

    /* 直接使用触摸坐标 */
    int32_t x = last_x;
    int32_t y = last_y;
    if (x < 0) x = 0;
    if (x >= UI_HOR_RES) x = UI_HOR_RES - 1;
    if (y < 0) y = 0;
    if (y >= UI_VER_RES) y = UI_VER_RES - 1;

    /* 设置最后按下的坐标 */
    data->point.x = x;
    data->point.y = y;
}
