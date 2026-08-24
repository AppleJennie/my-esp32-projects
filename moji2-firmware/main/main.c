/**
 * Moji 2.0 桌面机器人 —— 主程序
 *
 * 功能：
 *  - ST77916 圆屏显示机器人表情（周期性眨眼）
 *  - BOOT 键短按切换表情，RGB 灯随表情变色
 *  - 顶部电量图标（ADC 采集 VBAT）
 *  - Wi-Fi 连接成功后播放提示音并短暂露出“聆听”表情
 *  - ES8311 音频通路初始化 + 开机提示音
 *
 * 语音对话（小智 AI）接入说明见 README.md。
 */
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "board.h"
#include "bsp_display.h"
#include "bsp_audio.h"
#include "bsp_power.h"
#include "bsp_led.h"
#include "wifi_mgr.h"
#include "emoji.h"

static const char *TAG = "moji2";

#define LOOP_TICK_MS        30
#define BATT_INTERVAL_MS    2000

/* 表情对应的 RGB 灯颜色 */
static const struct {
    uint8_t r, g, b;
} s_expr_led[EMOJI_EXPR_COUNT] = {
    [EMOJI_NEUTRAL]   = { 0,  0,  0},     /* 灭 */
    [EMOJI_HAPPY]     = { 0, 60,  0},     /* 绿 */
    [EMOJI_ANGRY]     = {80,  0,  0},     /* 红 */
    [EMOJI_SLEEPY]    = {40, 25,  0},     /* 暖黄 */
    [EMOJI_LISTENING] = { 0, 25, 80},     /* 蓝 */
};

static volatile bool s_wifi_event = false;
static volatile bool s_wifi_ok = false;

static void on_wifi(bool connected)
{
    s_wifi_ok = connected;
    s_wifi_event = true;    /* 在主循环里播放提示音，避免阻塞事件任务 */
}

static void button_init(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_BOOT_BTN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

/* 短按检测（带消抖），返回 true 表示一次完整按下-释放 */
static bool button_clicked(void)
{
    static int low_count = 0;
    static bool pressed = false;

    if (gpio_get_level(PIN_BOOT_BTN) == 0) {
        if (++low_count >= 3) pressed = true;       /* 约 90ms 消抖 */
    } else {
        low_count = 0;
        if (pressed) {
            pressed = false;
            return true;
        }
    }
    return false;
}

void app_main(void)
{
    /* NVS（Wi-Fi 校准数据等需要） */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(bsp_power_init());
    ESP_ERROR_CHECK(bsp_display_init());
    ESP_ERROR_CHECK(emoji_init());
    ESP_ERROR_CHECK(bsp_audio_init());
    ESP_ERROR_CHECK(bsp_led_init());
    button_init();

    emoji_scene_t scene = {
        .expr = EMOJI_NEUTRAL,
        .eye_open = 1.0f,
        .battery_pct = bsp_power_battery_percent(),
    };
    emoji_draw(&scene);
    bsp_audio_play_boot_sound();

    wifi_mgr_start(on_wifi);

    ESP_LOGI(TAG, "启动完成，电量 %.2fV (%d%%)",
             bsp_power_battery_voltage(), scene.battery_pct);

    uint32_t tick = 0;
    uint32_t next_blink = 3000;
    float eye_target = 1.0f;
    uint32_t listen_until = 0;
    emoji_expr_t user_expr = EMOJI_NEUTRAL;

    while (1) {
        tick += LOOP_TICK_MS;

        /* Wi-Fi 事件：提示音 + 短暂“聆听”表情 */
        if (s_wifi_event) {
            s_wifi_event = false;
            if (s_wifi_ok) {
                listen_until = tick + 2000;
                bsp_audio_play_tone(1568, 150, 0.25f);
            }
        }

        /* BOOT 键切换表情 */
        if (button_clicked()) {
            user_expr = (user_expr + 1) % EMOJI_EXPR_COUNT;
            bsp_led_set(s_expr_led[user_expr].r, s_expr_led[user_expr].g, s_expr_led[user_expr].b);
        }

        /* 眨眼：每 2.5~4.5 秒快速闭眼一次 */
        if (tick >= next_blink) {
            eye_target = 0.05f;
            next_blink = tick + 2500 + (uint32_t)(rand() % 2000);
        }
        if (eye_target < 1.0f && scene.eye_open <= 0.08f) {
            eye_target = 1.0f;      /* 闭到位后重新睁开 */
        }
        scene.eye_open += (eye_target - scene.eye_open) * 0.4f;

        /* “聆听”表情优先于用户表情 */
        scene.expr = (tick < listen_until) ? EMOJI_LISTENING : user_expr;

        /* 定期更新电量 */
        if (tick % BATT_INTERVAL_MS < LOOP_TICK_MS) {
            scene.battery_pct = bsp_power_battery_percent();
        }

        emoji_draw(&scene);
        vTaskDelay(pdMS_TO_TICKS(LOOP_TICK_MS));
    }
}
