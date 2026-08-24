/**
 * Moji 2.0 板级引脚定义
 *
 * 引脚提取自官方原理图《原理图_Moji2_2026-01-24.pdf》Pin Map，
 * 并已与作者官方固件（MoveCall/xiaozhi-esp32 分支 movecall-moji2-esp32c5
 * 的 config.h）逐一对照确认：
 *
 *   IO0  SPI_SCK      IO9  SPI_D0       IO23 I2S_DOUT(喇叭)
 *   IO1  SPI_RESET    IO10 LED(WS2812)  IO24 I2S_LRCK
 *   IO2  LED_BL       IO11 I2S_SCLK     IO25 I2S_MCLK
 *   IO3  SPI_CS       IO12 I2S_DIN(麦克) IO26 I2C_SDA
 *   IO4  ADC(电池)    IO13 USB_N        IO27 I2C_SCL
 *   IO5  CTRL(功放)   IO14 USB_P        IO28 BOOT 按键
 *   IO6  SPI_D3
 *   IO7  SPI_D2
 *   IO8  SPI_D1
 */
#pragma once

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 屏幕：1.5 寸 360x360 圆屏，ST77916，QSPI 接口 ---------- */
#define PIN_LCD_SCK     GPIO_NUM_0   /* SPI_SCK  */
#define PIN_LCD_RST     GPIO_NUM_1   /* SPI_RESET */
#define PIN_LCD_BL      GPIO_NUM_2   /* LED_BL，经 Q2 驱动背光，高电平点亮 */
#define PIN_LCD_CS      GPIO_NUM_3   /* SPI_CS   */
#define PIN_LCD_D3      GPIO_NUM_6   /* SPI_D3   */
#define PIN_LCD_D2      GPIO_NUM_7   /* SPI_D2   */
#define PIN_LCD_D1      GPIO_NUM_8   /* SPI_D1   */
#define PIN_LCD_D0      GPIO_NUM_9   /* SPI_D0   */

#define LCD_H_RES       360
#define LCD_V_RES       360

/* ---------- 音频：ES8311 Codec + D 类功放 ---------- */
#define PIN_I2S_SCLK    GPIO_NUM_11  /* I2S BCLK */
#define PIN_I2S_DOUT    GPIO_NUM_23  /* ESP -> ES8311 DAC（喇叭通路）*/
#define PIN_I2S_DIN     GPIO_NUM_12  /* ES8311 ADC -> ESP（麦克风通路）*/
#define PIN_I2S_LRCK    GPIO_NUM_24  /* I2S WS   */
#define PIN_I2S_MCLK    GPIO_NUM_25  /* I2S MCLK */
#define PIN_PA_CTRL     GPIO_NUM_5   /* 功放 CTRL，高电平使能 */

#define PIN_I2C_SDA     GPIO_NUM_26
#define PIN_I2C_SCL     GPIO_NUM_27
#define I2C_PORT_NUM    I2C_NUM_0
#define ES8311_I2C_ADDR 0x18         /* CE 拉高时 7bit 地址为 0x18 */

#define AUDIO_SAMPLE_RATE   24000    /* 与官方小智固件一致，MCLK = 256 x Fs */

/* ---------- 电源 ---------- */
#define PIN_BAT_ADC     GPIO_NUM_4   /* VBAT 经 R6/R7 (100k/100k) 分压 */
#define BAT_ADC_CHANNEL ADC_CHANNEL_3   /* ESP32-C5: GPIO4 = ADC1_CH3 */
#define BAT_DIVIDER_RATIO 2.0f

/* ---------- 交互 ---------- */
#define PIN_RGB_LED     GPIO_NUM_10  /* TX1812 (WS2812 兼容)，单颗 */
#define PIN_BOOT_BTN    GPIO_NUM_28  /* BOOT 键，外部 10k 上拉，按下为低 */

/* USB 直接连接 ESP32-C5 内置 USB-Serial-JTAG：IO13=DN, IO14=DP，无需配置 */

#ifdef __cplusplus
}
#endif
