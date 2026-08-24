#include "bsp_display.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_st77916.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"

#include "board.h"
#include "st77916_init_cmds.h"

static const char *TAG = "display";

/* 一次 DMA 传输的最大字节数：整屏 1/4 高度的像素 */
#define LCD_MAX_TRANSFER    (LCD_H_RES * (LCD_V_RES / 4) * sizeof(uint16_t))

#define LCD_SPI_HOST        SPI2_HOST
#define BACKLIGHT_LEDC_CH   LEDC_CHANNEL_0

static esp_lcd_panel_handle_t s_panel = NULL;

static esp_err_t backlight_init(void)
{
    const ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "ledc timer failed");

    const ledc_channel_config_t ch_cfg = {
        .gpio_num   = PIN_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = BACKLIGHT_LEDC_CH,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 0,
        .hpoint     = 0,
    };
    return ledc_channel_config(&ch_cfg);
}

esp_err_t bsp_display_init(void)
{
    ESP_LOGI(TAG, "初始化 QSPI 总线");
    const spi_bus_config_t bus_cfg = ST77916_PANEL_BUS_QSPI_CONFIG(
        PIN_LCD_SCK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3,
        LCD_MAX_TRANSFER);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
                        TAG, "spi bus init failed");

    ESP_LOGI(TAG, "安装 QSPI Panel IO（命令单线、像素四线）");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_cfg =
        ST77916_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, NULL, NULL);
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_cfg, &io_handle),
        TAG, "panel io init failed");

    ESP_LOGI(TAG, "安装 ST77916 面板驱动（使用官方屏幕初始化序列）");
    st77916_vendor_config_t vendor_cfg = {
        .init_cmds = moji2_lcd_init_cmds,
        .init_cmds_size = sizeof(moji2_lcd_init_cmds) / sizeof(st77916_lcd_init_cmd_t),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config  = &vendor_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st77916(io_handle, &panel_cfg, &s_panel),
                        TAG, "panel init failed");

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    /* 部分批次的屏幕需要反色才正常，若颜色发灰/反相请取消下一行注释 */
    // ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight init failed");
    bsp_display_backlight_set(CONFIG_MOJI_LCD_BACKLIGHT);

    ESP_LOGI(TAG, "屏幕初始化完成");
    return ESP_OK;
}

void bsp_display_flush(const void *data, int y0, int y1)
{
    /* esp_lcd_panel_draw_bitmap 的 x_end/y_end 为开区间 */
    esp_lcd_panel_draw_bitmap(s_panel, 0, y0, LCD_H_RES, y1, data);
}

void bsp_display_backlight_set(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    uint32_t duty = ((1 << 10) - 1) * percent / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_LEDC_CH, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_LEDC_CH);
}
