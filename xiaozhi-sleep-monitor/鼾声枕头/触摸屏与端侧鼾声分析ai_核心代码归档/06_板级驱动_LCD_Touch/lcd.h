/**
 ****************************************************************************************************
 * @file        lcd.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2025-01-01
 * @brief       lcd驱动代码
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

#ifndef __LCD_H
#define __LCD_H

#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "rgblcd.h"
#include "xl9555.h"
#include <math.h>
#include <string.h>


/* 注意：以RGBLCD为主，如RGBLCD和MIPI LCD插入到开发板上，则默认选择RGBLCD */
/* 首先读取RGBLCD ID，如没有接入RGBLCD，则读取MIPI LCD */
#define GPIO_LCD_ID1     (GPIO_NUM_14)
#define GPIO_LCD_ID2     (GPIO_NUM_18)
#define GPIO_LCD_ID3     (GPIO_NUM_6)

/* RGB_BL */
#define LCD_BL(x)       do { x ?                                 \
                             xl9555_pin_write(LCD_BL_IO, 1):     \
                             xl9555_pin_write(LCD_BL_IO, 0);     \
                        } while(0)

/* 常用颜色值 */
#define WHITE           0xFFFF      /* 白色 */
#define BLACK           0x0000      /* 黑色 */
#define RED             0xF800      /* 红色 */
#define GREEN           0x07E0      /* 绿色 */
#define BLUE            0x001F      /* 蓝色 */ 
#define MAGENTA         0XF81F      /* 洋红色 */
#define YELLOW          0XFFE0      /* 黄色 */
#define CYAN            0X07FF      /* 蓝绿色 */

/* 非常用颜色 */
#define BROWN           0XBC40      /* 棕色 */
#define BRRED           0XFC07      /* 棕红色 */
#define GRAY            0X8430      /* 灰色 */ 
#define DARKBLUE        0X01CF      /* 深蓝色 */
#define LIGHTBLUE       0X7D7C      /* 浅蓝色 */ 
#define GRAYBLUE        0X5458      /* 灰蓝色 */ 
#define LIGHTGREEN      0X841F      /* 浅绿色 */  
#define LGRAY           0XC618      /* 浅灰色(PANNEL),窗体背景色 */ 
#define LGRAYBLUE       0XA651      /* 浅灰蓝色(中间层颜色) */ 
#define LBBLUE          0X2B12      /* 浅棕蓝色(选择条目的反色) */ 

/* LCD重要参数集 */
typedef struct  
{
    uint16_t id;                                /* 读取ID */
    uint32_t width;                             /* 面板的宽度,固定参数,不随显示方向改变 */
    uint32_t height;                            /* 面板的高度,固定参数,不随显示方向改变 */
    uint8_t  dir;                               /* 0,竖屏(MIPI只能竖屏);1,横屏; */
    uint8_t  color_byte;                        /* 颜色格式 */
    esp_lcd_panel_handle_t lcd_panel_handle;    /* LCD控制句柄 */
    esp_lcd_panel_io_handle_t lcd_dbi_io;       /* LCD IO控制句柄 */
    struct
    {
        int lcd_rst;                            /* 复位引脚 */
        int lcd_bl;                             /* 背光引脚 */
    } ctrl;
} _lcd_dev;

/* LCD参数 */
extern _lcd_dev lcddev;                         /* 管理LCD重要参数 */

/* 函数声明 */
void lcd_init(void);                                                                            /* lcd初始化函数 */
void lcd_clear(uint16_t color);                                                                 /* 清屏函数 */
void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color);                                    /* 画点函数 */
void lcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t color);              /* 填充函数 */
void lcd_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color);       /* 填充颜色块函数 */
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);         /* 画线函数 */
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t color);                      /* 画水平线函数 */
void lcd_draw_rectangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,uint16_t color);     /* 画矩形函数 */
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r, uint16_t color);                      /* 画圆函数 */
void lcd_fill_circle(uint16_t center_x, uint16_t center_y, uint16_t radius, uint16_t color);    /* 画实心圆函数 */
void lcd_show_char(uint16_t x, uint16_t y, char chr, uint8_t size, uint8_t mode, uint16_t color);       /* 显示字符函数 */
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint16_t color);     /* 显示数字函数 */
void lcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint16_t color);      /* 扩展显示len个数字 */
void lcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint16_t color);   /* 显示字符串函数 */

#endif
