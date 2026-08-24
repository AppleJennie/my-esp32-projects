/**
 ****************************************************************************************************
 * @file        lcd.c
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

#include "lcd.h"
#include "lcdfont.h"


DRAM_ATTR void *lcd_buffer[2];                /* 指向屏幕双缓存 */
DRAM_ATTR uint8_t buffer_sw = 0;              /* 当前使用的缓冲区索引 */
DRAM_ATTR uint8_t refresh_done_flag = 0;      /* 缓存切换索引 */
DRAM_ATTR _lcd_dev lcddev;                    /* 管理LCD重要参数 */
uint32_t g_back_color  = 0xFFFF;    /* 背景色 */

/**
 * @brief       读取RGB LCD ID
 * @note        利用LCD RGB线的最高位(R7,G7,B7)来识别面板ID
 *              IO14 = R7(M0); IO8 = G7(M1); IO3 = B7(M2);
 *              M2:M1:M0
 *              0 :0 :0     4.3 寸480*272  RGB屏,ID = 0X4342
 *              0 :0 :1     7   寸800*480  RGB屏,ID = 0X7084
 *              0 :1 :0     7   寸1024*600 RGB屏,ID = 0X7016
 *              0 :1 :1     7   寸1280*800 RGB屏,ID = 0X7018
 *              1 :0 :0     4.3 寸800*480  RGB屏,ID = 0X4348
 *              1 :0 :1     10.1寸1280*800 RGB屏,ID = 0X1018
 * 
 * @param       无
 * @retval      0, 非法; 
 *              其他, LCD ID
 */
uint16_t lcd_panelid_read(void)
{
    uint8_t idx = 0;
    gpio_config_t gpio_init_struct = {0};

    gpio_init_struct.intr_type      = GPIO_INTR_DISABLE;        /* 失能引脚中断 */
    gpio_init_struct.mode           = GPIO_MODE_INPUT;          /* 输入输出模式 */
    gpio_init_struct.pull_up_en     = GPIO_PULLUP_ENABLE;       /* 使能上拉 */
    gpio_init_struct.pull_down_en   = GPIO_PULLDOWN_DISABLE;    /* 失能下拉 */
    gpio_init_struct.pin_bit_mask   = 1ull << GPIO_LCD_R7 | 1ull << GPIO_LCD_G7 | 1ull << GPIO_LCD_B7;
    gpio_config(&gpio_init_struct);                             /* 配置GPIO */

    idx  = (uint8_t)gpio_get_level(GPIO_LCD_R7);        /* 读取M0 */
    idx |= (uint8_t)gpio_get_level(GPIO_LCD_G7) << 1;   /* 读取M1 */
    idx |= (uint8_t)gpio_get_level(GPIO_LCD_B7) << 2;   /* 读取M2 */

    /* 正点原子其他的RGB LCD自行匹配 */
    switch (idx)
    {
        case 0:
        {
            return 0x4342;                      /* ATK-MD0430R-480272 */
        }
        // case 1:
        // {
        //     return 0x7084;                      /* ATK-MD0700R-800480 */
        // }
        case 4:
        {
            return 0x4384;                      /* ATK-MD0430R-800480 */
        }
        default:
        {
            return 0;
        }
    }
}

/**
 * @brief       内部缓存刷新完成回调函数
 * @param       panel_io: RGBLCD IO的句柄
 * @param       edata: 事件数据类型
 * @param       user_ctx: 传入参数
 * @retval      无
 */
IRAM_ATTR static bool lcd_rgb_panel_refresh_done_callback(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx)
{
    refresh_done_flag = 1;
    return false;
}

/**
 * @brief       初始化LCD
 * @param       无
 * @retval      无
 */
void lcd_init(void)
{
    lcddev.id = lcd_panelid_read();                             /* 读取RGB LCD面板ID */
    lcddev.ctrl.lcd_rst = GPIO_NUM_NC;                          /* 复位管脚 */
    lcddev.ctrl.lcd_bl = GPIO_NUM_NC;                           /* 背光管脚 */

    // gpio_config_t gpio_init_struct = {0};
    // gpio_init_struct.intr_type    = GPIO_INTR_DISABLE;          /* 失能引脚中断 */
    // gpio_init_struct.mode         = GPIO_MODE_OUTPUT;           /* 输出模式 */
    // gpio_init_struct.pull_up_en   = GPIO_PULLUP_DISABLE;        /* 失能上拉 */
    // gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;      /* 失能下拉 */
    // gpio_init_struct.pin_bit_mask = 1ull << lcddev.ctrl.lcd_bl; /* 设置的引脚的位掩码 */
    // ESP_ERROR_CHECK(gpio_config(&gpio_init_struct));            /* 配置GPIO */

    LCD_BL(0);      /* 背光关闭 */

    lcddev.lcd_panel_handle = rgblcd_init();                    /* 初始化RGB LCD */
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(lcddev.lcd_panel_handle, 2, &lcd_buffer[0], &lcd_buffer[1]));
    
    const esp_lcd_rgb_panel_event_callbacks_t rgb_cbs = {
        .on_bounce_frame_finish = lcd_rgb_panel_refresh_done_callback,  /* 内部缓冲区刷新完成回调函数 */
    };

    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(lcddev.lcd_panel_handle, &rgb_cbs, NULL));
    lcd_clear(WHITE);

    LCD_BL(1);      /* 打开背光 */
}

/**
 * @brief       清屏
 * @param       color :清屏颜色
 * @retval      无
 */
IRAM_ATTR void lcd_clear(uint16_t color)
{
    uint16_t *buffer = (uint16_t *)lcd_buffer[buffer_sw];  /* 将 void* 转换为 uint16_t* */

    /* 制定缓存区填充颜色值 */
    for (uint32_t i = 0; i < lcddev.width * lcddev.height; i++)
    {
        buffer[i] = color;
    }

    esp_lcd_panel_draw_bitmap(lcddev.lcd_panel_handle, 0, 0, lcddev.width, lcddev.height, buffer);
    /* 清除传输完成标志 */
    refresh_done_flag = 0;

    do
    {
        /* 等待内部缓存刷新完成 */
        vTaskDelay(1);
    }
    while (refresh_done_flag != 1);
    /* 使用异或操作在 0 和 1 之间切换，目的是为了切换另一个缓冲区 */
    buffer_sw ^= 1;
}

/**
 * @brief       画点函数
 * @param       x,y   :写入坐标
 * @param       color :颜色值
 * @retval      无
 */
void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color)
{
    esp_lcd_panel_draw_bitmap(lcddev.lcd_panel_handle, x, y, x + 1, y + 1, (uint16_t *)&color);
}

/**
 * @brief       在指定区域内填充单个颜色
 * @note        此函数仅支持uint16_t,RGB565格式的颜色数组填充.
 *              (sx,sy),(ex,ey):填充矩形对角坐标,区域大小为:(ex - sx + 1) * (ey - sy + 1)
 *              注意:sx,ex,不能大于lcddev.width - 1; sy,ey,不能大于lcddev.height - 1
 * @param       sx,sy:起始坐标
 * @param       ex,ey:结束坐标
 * @param       color:要填充的颜色
 * @retval      无
 */
void lcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t color)
{
    /* 确保坐标在合法范围内 */
    if (sx >= lcddev.width || sy >= lcddev.height || ex > lcddev.width || ey > lcddev.height || sx >= ex || sy >= ey)
    {
        ESP_LOGE("TAG", "Invalid fill area");
        return;
    }

    /* 计算填充区域的宽度 */
    uint16_t width = ex - sx;
    uint16_t height = ey - sy;

    /* 分配内存 */
    uint16_t *buffer = heap_caps_malloc(width * sizeof(uint16_t), MALLOC_CAP_INTERNAL);
    
    if (NULL == buffer)
    {
        ESP_LOGE("TAG", "Memory for bitmap is not enough");
    }
    else
    {
        /* 填充颜色 */
        for (uint16_t i = 0; i < width; i++)
        {
            buffer[i] = color;
        }

        /* 绘制填充区域 */
        for (uint16_t y = 0; y < height; y++)
        {
            esp_lcd_panel_draw_bitmap(lcddev.lcd_panel_handle, sx, sy + y, ex, sy + y + 1, buffer);
        }
    }

    /* 释放内存 */
    heap_caps_free(buffer);
}

/**
 * @brief       在指定区域内填充指定颜色块
 * @param       (sx,sy),(ex,ey):填充矩形对角坐标,区域大小为:(ex - sx + 1) * (ey - sy + 1)
 * @param       color: 要填充的颜色数组首地址
 * @retval      无
 */
void lcd_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color)
{
    /* 确保坐标在合法范围内 */
    if (sx >= lcddev.width || sy >= lcddev.height || ex > lcddev.width || ey > lcddev.height || sx >= ex || sy >= ey)
    {
        ESP_LOGE("TAG", "Invalid fill area");
        return;
    }

    /* 计算填充区域的宽度 */
    uint16_t width = ex - sx + 1;
    uint16_t height = ey - sy + 1;
    uint32_t buf_index = 0;

    uint16_t *buffer = heap_caps_malloc(width * sizeof(uint16_t), MALLOC_CAP_INTERNAL);

    for (uint16_t y_index = 0; y_index < height; y_index++)
    {
        for (uint16_t x_index = 0; x_index < width ; x_index++)
        {
            buffer[x_index] = color[buf_index];
            buf_index++;
        }

        esp_lcd_panel_draw_bitmap(lcddev.lcd_panel_handle, sx, sy + y_index, ex, sy + 1 + y_index, buffer);
    }

    /* 释放内存 */
    heap_caps_free(buffer);
}

/**
 * @brief       画线
 * @param       x1,y1:起点坐标
 * @param       x2,y2:终点坐标
 * @param       color:线的颜色
 * @retval      无
 */
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    uint16_t t;
    int xerr = 0, yerr = 0, delta_x, delta_y, distance;
    int incx, incy, row, col;
    delta_x = x2 - x1;      /* 计算坐标增量 */
    delta_y = y2 - y1;
    row = x1;
    col = y1;

    if (delta_x > 0)
    {
        incx = 1;           /* 设置单步方向 */
    }
    else if (delta_x == 0)
    {
        incx = 0;           /* 垂直线 */
    }
    else
    {
        incx = -1;
        delta_x = -delta_x;
    }

    if (delta_y > 0)
    {
        incy = 1;
    }
    else if (delta_y == 0)
    {
        incy = 0;            /* 水平线 */
    }
    else
    {
        incy = -1;
        delta_y = -delta_y;
    }

    if ( delta_x > delta_y)
    {
        distance = delta_x; /* 选取基本增量坐标轴 */
    }
    else
    {
        distance = delta_y;
    }

    for (t = 0; t <= distance + 1; t++)     /* 画线输出 */
    {
        lcd_draw_point(row, col, color);    /* 画点 */
        xerr += delta_x;
        yerr += delta_y;

        if (xerr > distance)
        {
            xerr -= distance;
            row += incx;
        }

        if (yerr > distance)
        {
            yerr -= distance;
            col += incy;
        }
    }
}

/**
 * @brief       画水平线
 * @param       x,y   : 起点坐标
 * @param       len   : 线长度
 * @param       color : 矩形的颜色
 * @retval      无
 */
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t color)
{
    /* 确保坐标在LCD范围内 */
    if (len == 0 || x >= lcddev.width || y >= lcddev.height) return;

    uint16_t ex = fmin(lcddev.width - 1, x + len - 1);
    uint16_t ey = y;

    /* 填充颜色区域 */
    uint32_t width = ex - x + 1;
    uint32_t h = ey - y + 1;
    uint16_t *color_buffer = malloc(width * h * sizeof(uint16_t));
    if (color_buffer == NULL) return; /* 检查内存分配是否成功 */

    for (uint32_t i = 0; i < width * h; i++)
    {
        color_buffer[i] = color;
    }

    esp_lcd_panel_draw_bitmap(lcddev.lcd_panel_handle, x, y, ex + 1, ey + 1, color_buffer);
    free(color_buffer);
}

/**
 * @brief       画一个矩形
 * @param       x1,y1   起点坐标
 * @param       x2,y2   终点坐标
 * @param       color 填充颜色
 * @retval      无
 */
void lcd_draw_rectangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,uint16_t color)
{
    lcd_draw_line(x0, y0, x1, y0,color);
    lcd_draw_line(x0, y0, x0, y1,color);
    lcd_draw_line(x0, y1, x1, y1,color);
    lcd_draw_line(x1, y0, x1, y1,color);
}

/**
 * @brief       画圆
 * @param       x0,y0 : 圆中心坐标
 * @param       r     : 半径
 * @param       color : 圆的颜色
 * @retval      无
 */
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r, uint16_t color)
{
    int a, b;
    int di;

    a = 0;
    b = r;
    di = 3 - (r << 1);       /* 判断下个点位置的标志 */

    while (a <= b)
    {
        lcd_draw_point(x0 + a, y0 - b, color);  /* 5 */
        lcd_draw_point(x0 + b, y0 - a, color);  /* 0 */
        lcd_draw_point(x0 + b, y0 + a, color);  /* 4 */
        lcd_draw_point(x0 + a, y0 + b, color);  /* 6 */
        lcd_draw_point(x0 - a, y0 + b, color);  /* 1 */
        lcd_draw_point(x0 - b, y0 + a, color);
        lcd_draw_point(x0 - a, y0 - b, color);  /* 2 */
        lcd_draw_point(x0 - b, y0 - a, color);  /* 7 */
        a++;

        /* 使用Bresenham算法画圆 */
        if (di < 0)
        {
            di += 4 * a + 6;
        }
        else
        {
            di += 10 + 4 * (a - b);
            b--;
        }
    }
}

/**
 * @brief       绘制一个实体圆的函数
 * @param       center_x,center_y:写入坐标
 * @param       radius:圆的半径
 * @param       color:颜色值
 * @retval      无
 */
void lcd_fill_circle(uint16_t center_x, uint16_t center_y, uint16_t radius, uint16_t color)
{
    uint32_t i;
    uint32_t imax = ((uint32_t)radius * 707) / 1000 + 1;
    uint32_t sqmax = (uint32_t)radius * (uint32_t)radius + (uint32_t)radius / 2;
    uint32_t xr = radius;

    lcd_draw_hline(center_x - radius, center_y, 2 * radius, color);

    for (i = 1; i <= imax; i++)
    {
        if ((i * i + xr * xr) > sqmax)
        {
            /* draw lines from outside */
            if (xr > imax)
            {
                lcd_draw_hline (center_x - i + 1, center_y + xr, 2 * (i - 1), color);
                lcd_draw_hline (center_x - i + 1, center_y - xr, 2 * (i - 1), color);
            }

            xr--;
        }

        /* draw lines from inside (center) */
        lcd_draw_hline(center_x - xr, center_y + i, 2 * xr, color);
        lcd_draw_hline(center_x - xr, center_y - i, 2 * xr, color);
    }
}

/**
 * @brief       在指定位置显示一个字符
 * @param       x,y  :坐标
 * @param       chr  :要显示的字符:" "--->"~"
 * @param       size :字体大小 12/16/24/32
 * @param       mode :叠加方式(1); 非叠加方式(0);
 * @param       color:字体颜色
 * @retval      无
 */
void lcd_show_char(uint16_t x, uint16_t y, char chr, uint8_t size, uint8_t mode, uint16_t color)
{
    uint8_t temp, t1, t;
    uint16_t y0 = y;
    uint8_t csize = 0;
    uint8_t *pfont = 0;

    csize = (size / 8 + ((size % 8) ? 1 : 0)) * (size / 2); /* 得到字体一个字符对应点阵集所占的字节数 */
    chr = (char)chr - ' ';      /* 得到偏移后的值（ASCII字库是从空格开始取模，所以-' '就是对应字符的字库） */

    switch (size)
    {
        case 12:
            pfont = (uint8_t *)asc2_1206[(uint8_t)chr];     /* 调用1206字体 */
            break;

        case 16:
            pfont = (uint8_t *)asc2_1608[(uint8_t)chr];     /* 调用1608字体 */
            break;

        case 24:
            pfont = (uint8_t *)asc2_2412[(uint8_t)chr];     /* 调用2412字体 */
            break;

        case 32:
            pfont = (uint8_t *)asc2_3216[(uint8_t)chr];     /* 调用3216字体 */
            break;

        default:
            return ;
    }

    for (t = 0; t < csize; t++)
    {
        temp = pfont[t];                                    /* 获取字符的点阵数据 */

        for (t1 = 0; t1 < 8; t1++)                          /* 一个字节8个点 */
        {
            if (temp & 0x80)                                /* 有效点,需要显示 */
            {
                lcd_draw_point(x, y, color);                /* 画点出来,要显示这个点 */
            }
            else if (mode == 0)                             /* 无效点,不显示 */
            {
                lcd_draw_point(x, y, g_back_color);         /* 画背景色,相当于这个点不显示(注意背景色由全局变量控制) */
            }

            temp <<= 1;                                     /* 移位, 以便获取下一个位的状态 */
            y++;

            if (y >= lcddev.height) return;                 /* 超区域了 */

            if ((y - y0) == size)                           /* 显示完一列了? */
            {
                y = y0;                                     /* y坐标复位 */
                x++;                                        /* x坐标递增 */
                
                if (x >= lcddev.width)
                {
                    return;                                 /* x坐标超区域了 */
                }

                break;
            }
        }
    }
}

/**
 * @brief       平方函数, m^n
 * @param       m:底数
 * @param       n:指数
 * @retval      m的n次方
 */
static uint32_t lcd_pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;

    while (n--)
    {
        result *= m;
    }

    return result;
}

/**
 * @brief       显示len个数字
 * @param       x,y     :起始坐标
 * @param       num     :数值(0 ~ 2^32)
 * @param       len     :显示数字的位数
 * @param       size    :选择字体 12/16/24/32
 * @param       color   :字体颜色
 * @retval      无
 */
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint16_t color)
{
    uint8_t t, temp;
    uint8_t enshow = 0;

    for (t = 0; t < len; t++)                                               /* 按总显示位数循环 */
    {
        temp = (num / lcd_pow(10, len - t - 1)) % 10;                       /* 获取对应位的数字 */

        if (enshow == 0 && t < (len - 1))                                   /* 没有使能显示,且还有位要显示 */
        {
            if (temp == 0)
            {
                lcd_show_char(x + (size / 2) * t, y, ' ', size, 0, color);  /* 显示空格,占位 */
                continue;                                                   /* 继续下个一位 */
            }
            else
            {
                enshow = 1;                                                 /* 使能显示 */
            }
        }

        lcd_show_char(x + (size / 2) * t, y, temp + '0', size, 0, color);   /* 显示字符 */
    }
}

/**
 * @brief       扩展显示len个数字(高位是0也显示)
 * @param       x,y     :起始坐标
 * @param       num     :数值(0 ~ 2^32)
 * @param       len     :显示数字的位数
 * @param       size    :选择字体 12/16/24/32
 * @param       mode    :显示模式
 *              [7]:0,不填充;1,填充0.
 *              [6:1]:保留
 *              [0]:0,非叠加显示;1,叠加显示.
 * @param       color   :字体颜色
 * @retval      无
 */
void lcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint16_t color)
{
    uint8_t t, temp;
    uint8_t enshow = 0;

    for (t = 0; t < len; t++)                                                               /* 按总显示位数循环 */
    {
        temp = (num / lcd_pow(10, len - t - 1)) % 10;                                       /* 获取对应位的数字 */

        if (enshow == 0 && t < (len - 1))                                                   /* 没有使能显示,且还有位要显示 */
        {
            if (temp == 0)
            {
                if (mode & 0x80)                                                            /* 高位需要填充0 */
                {
                    lcd_show_char(x + (size / 2) * t, y, '0', size, mode & 0x01, color);    /* 用0占位 */
                }
                else
                {
                    lcd_show_char(x + (size / 2) * t, y, ' ', size, mode & 0x01, color);    /* 用空格占位 */
                }

                continue;
            }
            else
            {
                enshow = 1;                                                                 /* 使能显示 */
            }

        }

        lcd_show_char(x + (size / 2) * t, y, temp + '0', size, mode & 0x01, color);
    }
}

/**
 * @brief       显示字符串
 * @param       x,y         :起始坐标
 * @param       width,height:区域大小
 * @param       size        :选择字体 12/16/24/32
 * @param       p           :字符串首地址
 * @param       color       :字体颜色
 * @retval      无
 */
void lcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint16_t color)
{
    uint8_t x0 = x;
    
    width += x;
    height += y;

    while ((*p <= '~') && (*p >= ' '))   /* 判断是不是非法字符! */
    {
        if (x >= width)
        {
            x = x0;
            y += size;
        }

        if (y >= height)
        {
            break;                       /* 退出 */
        }

        lcd_show_char(x, y, *p, size, 0, color);
        x += size / 2;
        p++;
    }
}
