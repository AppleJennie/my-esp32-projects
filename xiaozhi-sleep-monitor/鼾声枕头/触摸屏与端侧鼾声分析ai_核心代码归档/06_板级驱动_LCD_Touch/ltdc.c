/**
 ****************************************************************************************************
 * @file        ltdc.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2023-12-1
 * @brief       LTDC 驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 ESP32S3 开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#include "ltdc.h"
#include "ltdcfont.h"


static const char *TAG = "ltdc";
esp_lcd_panel_handle_t panel_handle = NULL;                         /* RGBLCD句柄 */
static portMUX_TYPE my_spinlock = portMUX_INITIALIZER_UNLOCKED;     /* 定义portMUX_TYPE类型的自旋锁变量,用于临界区保护 */
uint32_t g_back_color  = 0xFFFF;                                    /* 背景色 */

/* 管理LTDC重要参数 */
_ltdc_dev ltdcdev;

/**
 * @brief       LTDC读取面板ID
 * @note        利用LCD RGB线的最高位(R7,G7,B7)来识别面板ID
 *              PG6 = R7(M0); PI2 = G7(M1); PI7 = B7(M2);
 *              M2:M1:M0
 *              0 :0 :0     4.3 寸480*272  RGB屏,ID = 0X4342
 *              1 :0 :0     4.3 寸800*480  RGB屏,ID = 0X4384
 * @param       无
 * @retval      0, 非法; 
 *              其他, LCD ID
 */
uint16_t ltdc_panelid_read(void)
{
    uint8_t idx = 0;
    gpio_config_t gpio_init_struct = {0};

    gpio_init_struct.intr_type = GPIO_INTR_DISABLE;         /* 失能引脚中断 */
    gpio_init_struct.mode = GPIO_MODE_INPUT;                /* 输入输出模式 */
    gpio_init_struct.pull_up_en = GPIO_PULLUP_ENABLE;       /* 使能上拉 */
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;  /* 失能下拉 */
    gpio_init_struct.pin_bit_mask = 1ull << GPIO_LCD_B7;
    gpio_config(&gpio_init_struct);                         /* 配置GPIO */

    gpio_init_struct.pin_bit_mask = 1ull << GPIO_LCD_R7 || 1ull << GPIO_LCD_G7;
    gpio_config(&gpio_init_struct);                         /* 配置GPIO */

    idx  = (uint8_t)gpio_get_level(GPIO_LCD_R7);            /* 读取M0 */
    idx |= (uint8_t)gpio_get_level(GPIO_LCD_G7) << 1;       /* 读取M1 */
    idx |= (uint8_t)gpio_get_level(GPIO_LCD_B7) << 2;       /* 读取M2 */

    switch (idx)
    {
        case 0 : 
            return 0X4342;                                  /* 4.3寸屏, 480*272分辨率 */
        case 4 : 
            return 0X4384;                                  /* 4.3寸屏, 800*480分辨率 */
        default : 
            return 0;
    }
}

/**
 * @brief       初始化ltdc
 * @param       无
 * @retval      无
 */
void ltdc_init(void)
{
    panel_handle = NULL;
    ltdcdev.id = ltdc_panelid_read();           /* 读取LCD面板ID */

    if (ltdcdev.id == 0X4342)                   /* 4.3寸屏, 480*272 RGB屏 */
    {
        ltdcdev.pwidth = 480;                   /* 面板宽度,单位:像素 */
        ltdcdev.pheight = 272;                  /* 面板高度,单位:像素 */
        ltdcdev.hsw = 1;                        /* 水平同步宽度 */
        ltdcdev.vsw = 1;                        /* 垂直同步宽度 */
        ltdcdev.hbp = 40;                       /* 水平后廊 */
        ltdcdev.vbp = 8;                        /* 垂直后廊 */
        ltdcdev.hfp = 5;                        /* 水平前廊 */
        ltdcdev.vfp = 8;                        /* 垂直前廊 */
        ltdcdev.pclk_hz = 9 * 1000 * 1000;      /* 设置像素时钟 9Mhz */
    }
    else if (ltdcdev.id == 0X4384)
    {
        ltdcdev.pwidth = 800;                   /* 面板宽度,单位:像素 */
        ltdcdev.pheight = 480;                  /* 面板高度,单位:像素 */
        ltdcdev.hbp = 88;                       /* 水平后廊 */
        ltdcdev.hfp = 40;                       /* 水平前廊 */
        ltdcdev.hsw = 48;                       /* 水平同步宽度 */
        ltdcdev.vbp = 32;                       /* 垂直后廊 */
        ltdcdev.vfp = 13;                       /* 垂直前廊 */
        ltdcdev.vsw = 3;                        /* 垂直同步宽度 */
        ltdcdev.pclk_hz = 18 * 1000 * 1000;     /* 设置像素时钟 18Mhz */
    }

    /* 配置RGB参数 */
    esp_lcd_rgb_panel_config_t panel_config = { /* RGBLCD配置结构体 */
        .data_width = 16,                       /* 数据宽度为16位 */
        .psram_trans_align = 64,                /* 在PSRAM中分配的缓冲区的对齐 */
        .clk_src = LCD_CLK_SRC_PLL160M,         /* RGBLCD外设时钟源 */
        .disp_gpio_num = GPIO_NUM_NC,           /* 用于显示控制信号,不使用设为-1 */
        .pclk_gpio_num = GPIO_LCD_PCLK,         /* PCLK信号引脚 */
        .hsync_gpio_num = GPIO_NUM_NC,          /* HSYNC信号引脚,DE模式可不使用 */
        .vsync_gpio_num = GPIO_NUM_NC,          /* VSYNC信号引脚,DE模式可不使用 */
        .de_gpio_num = GPIO_LCD_DE,             /* DE信号引脚 */
        .data_gpio_nums = {                     /* 数据线引脚 */
            GPIO_LCD_B3, GPIO_LCD_B4, GPIO_LCD_B5, GPIO_LCD_B6, GPIO_LCD_B7,
            GPIO_LCD_G2, GPIO_LCD_G3, GPIO_LCD_G4, GPIO_LCD_G5, GPIO_LCD_G6, GPIO_LCD_G7,
            GPIO_LCD_R3, GPIO_LCD_R4, GPIO_LCD_R5, GPIO_LCD_R6, GPIO_LCD_R7,
        },
        .timings = {                            /* RGBLCD时序参数 */
            .pclk_hz = ltdcdev.pclk_hz,         /* 像素时钟频率 */
            .h_res = ltdcdev.pwidth,            /* 水平分辨率，即一行中的像素数 */
            .v_res = ltdcdev.pheight,           /* 垂直分辨率，即帧中的行数 */
            .hsync_back_porch = ltdcdev.hbp,    /* 水平后廊，hsync和行活动数据开始之间的PCLK数 */
            .hsync_front_porch = ltdcdev.hfp,   /* 水平前廊，活动数据结束和下一个hsync之间的PCLK数 */
            .hsync_pulse_width = ltdcdev.vsw,   /* 垂直同步宽度，单位：行数 */
            .vsync_back_porch = ltdcdev.vbp,    /* 垂直后廊，vsync和帧开始之间的无效行数 */
            .vsync_front_porch = ltdcdev.vfp,   /* 垂直前廊，帧结束和下一个vsync之间的无效行数 */
            .vsync_pulse_width = ltdcdev.hsw,   /* 水平同步宽度，单位：PCLK周期 */
            .flags.pclk_active_neg = true,      /* RGB数据在下降沿计时 */
        },
        .flags.fb_in_psram = true,              /* 在PSRAM中分配帧缓冲区 */
        .bounce_buffer_size_px = (ltdcdev.id == 0X4384) ? 800 * 20 : 272 * 10,  /* 解决写spiflash时,抖动问题 */
    };

    /* 创建RGB对象 */
    esp_lcd_new_rgb_panel(&panel_config, &panel_handle);
    /* 复位RGB屏 */
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    /* 初始化RGB */
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    /* 设置横屏 */
    ltdc_display_dir(1);
    /* 清除屏幕为颜色 */
    ltdc_clear(WHITE);
    /* 打开背光 */
    LCD_BL(1);
}


/**
 * @brief       清除屏幕
 * @param       color:清除的颜色
 * @retval      无
 */
void ltdc_clear(uint16_t color)
{
    uint16_t *buffer = heap_caps_malloc(ltdcdev.width * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    if (NULL == buffer)
    {
        ESP_LOGE(TAG, "Memory for bitmap is not enough");
    }
    else
    {
        for (uint16_t i = 0; i < ltdcdev.width; i++)
        {
            buffer[i] = color;
        }
        
        for (uint16_t y = 0; y < ltdcdev.height; y++)
        {   /* 使用taskENTER_CRITICAL()和taskEXIT_CRITICAL()保护画点过程,禁止任务调度 */
            taskENTER_CRITICAL(&my_spinlock);   /* 屏蔽中断 */
            esp_lcd_panel_draw_bitmap(panel_handle, 0, y, ltdcdev.width, y + 1, buffer);
            taskEXIT_CRITICAL(&my_spinlock);    /* 重新使能中断 */
        }
        
        heap_caps_free(buffer);
    }
}

/**
 * @brief       RGB888转RGB565
 * @param       r:红色
 * @param       g:绿色
 * @param       b:蓝色
 * @retval      返回RGB565颜色值
 */
uint16_t ltdc_rgb888_to_565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

/**
 * @brief       LTDC显示方向设置
 * @param       dir:0,竖屏；1,横屏
 * @retval      无
 */
void ltdc_display_dir(uint8_t dir)
{
    ltdcdev.dir = dir;              /* 显示方向 */

    if (ltdcdev.dir == 0)           /* 竖屏 */
    {
        ltdcdev.width = ltdcdev.pheight;
        ltdcdev.height = ltdcdev.pwidth;
        esp_lcd_panel_swap_xy(panel_handle, true);          /* 交换X和Y轴 */ 
        esp_lcd_panel_mirror(panel_handle, false, true);    /* 对屏幕的Y轴进行镜像处理 */
    }
    else if (ltdcdev.dir == 1)      /* 横屏 */
    {
        ltdcdev.width = ltdcdev.pwidth;
        ltdcdev.height = ltdcdev.pheight;
        esp_lcd_panel_swap_xy(panel_handle, false);         /* 不需要交换X和Y轴 */
        esp_lcd_panel_mirror(panel_handle, false, false);   /* 对屏幕的XY轴不进行镜像处理 */
    }
}

/**
 * @brief       LTDC画点函数
 * @param       x,y     :写入坐标
 * @param       color   :颜色值
 * @retval      无
 */
void ltdc_draw_point(uint16_t x, uint16_t y, uint16_t color)
{
    taskENTER_CRITICAL(&my_spinlock);
    esp_lcd_panel_draw_bitmap(panel_handle, x, y, x + 1, y + 1, &color);
    taskEXIT_CRITICAL(&my_spinlock);
}

/**
 * @brief       在指定区域内填充指定颜色块
 * @note        此函数仅支持uint16_t,RGB565格式的颜色数组填充.
 *              (sx,sy),(ex,ey):填充矩形对角坐标,区域大小为:(ex - sx + 1) * (ey - sy + 1)
 *              注意:sx,ex,不能大于ltdcdev.width - 1; sy,ey,不能大于ltdcdev.height - 1
 * @param       sx,sy:起始坐标
 * @param       ex,ey:结束坐标
 * @param       color:填充的颜色数组首地址
 * @retval      无
 */
void ltdc_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t color)
{
    /* 确保坐标在LCD范围内 */
    if (sx < 0 || sy < 0 || ex > ltdcdev.width || ey > ltdcdev.height)
    {
        return; /* 坐标超出LCD范围，不执行填充 */
    }  
      
    /* 确保起始坐标小于结束坐标 */
    if (sx > ex || sy > ey)
    {
        return; /* 无效的填充区域，不执行填充 */
    }  
      
    /* 确保填充区域完全在LCD范围内 */
    sx = fmax(0, sx);
    sy = fmax(0, sy);
    ex = fmin(ltdcdev.width - 1, ex);
    ey = fmin(ltdcdev.height - 1, ey);
      
    /* 开始填充颜色 */
    for (int i = sx; i <= ex; i++)
    {
        for (int j = sy; j <= ey; j++)
        {
            /* 设置RGB值到LCD的对应位置 */
            ltdc_draw_point(i, j, color);
        }
    }
}

/**
 * @brief       显示单色图标
 * @param       x,y,width,height:坐标及尺寸
 * @param       icosbase:点整位置
 * @param       color:画点的颜色
 * @param       bkcolor:背景色
 * @retval      无
 */
void ltdc_app_show_mono_icos(uint16_t x,uint16_t y,uint8_t width,uint8_t height,uint8_t *icosbase,uint16_t color,uint16_t bkcolor)
{
    uint16_t rsize;
    uint16_t i,j;
    uint8_t temp;
    uint8_t t = 0;
    uint16_t x0 = x;//保留x的位置
    rsize = width / 8 + ((width % 8) ? 1 : 0);//每行的字节数

    for (i = 0;i < rsize * height;i++)
    {
        temp = icosbase[i];

        for(j = 0;j < 8;j++)
        {
            if (temp & 0x80)
            {
                ltdc_draw_point(x,y,color);
            }
            else
            {
                ltdc_draw_point(x,y,bkcolor);
            }

            temp <<= 1;
            x++;
            t++;        //宽度计数器

            if (t == width)
            {
                t=0;
                x=x0;
                y++;
                break;
            }
        }
    }
}

/**
 * @brief       画线
 * @param       x1,y1:起点坐标
 * @param       x2,y2:终点坐标
 * @param       color:线的颜色
 * @retval      无
 */
void ltdc_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
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
        ltdc_draw_point(row, col, color);   /* 画点 */
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
 * @brief       画一个矩形
 * @param       x1,y1   起点坐标
 * @param       x2,y2   终点坐标
 * @param       color 填充颜色
 * @retval      无
 */
void ltdc_draw_rectangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,uint16_t color)
{
    ltdc_draw_line(x0, y0, x1, y0,color);
    ltdc_draw_line(x0, y0, x0, y1,color);
    ltdc_draw_line(x0, y1, x1, y1,color);
    ltdc_draw_line(x1, y0, x1, y1,color);
}

/**
 * @brief       画圆
 * @param       x0,y0:圆中心坐标
 * @param       r    :半径
 * @param       color:圆的颜色
 * @retval      无
 */
void ltdc_draw_circle(uint16_t x0, uint16_t y0, uint8_t r, uint16_t color)
{
    int a, b;
    int di;
    a = 0;
    b = r;
    di = 3 - (r << 1);       /* 判断下个点位置的标志 */

    while (a <= b)
    {
        ltdc_draw_point(x0 + a, y0 - b, color);  /* 5 */
        ltdc_draw_point(x0 + b, y0 - a, color);  /* 0 */
        ltdc_draw_point(x0 + b, y0 + a, color);  /* 4 */
        ltdc_draw_point(x0 + a, y0 + b, color);  /* 6 */
        ltdc_draw_point(x0 - a, y0 + b, color);  /* 1 */
        ltdc_draw_point(x0 - b, y0 + a, color);
        ltdc_draw_point(x0 - a, y0 - b, color);  /* 2 */
        ltdc_draw_point(x0 - b, y0 - a, color);  /* 7 */
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
 * @brief       在指定位置显示一个字符
 * @param       x,y  :坐标
 * @param       chr  :要显示的字符:" "--->"~"
 * @param       size :字体大小 12/16/24/32
 * @param       mode :叠加方式(1); 非叠加方式(0);
 * @param       color:字体颜色
 * @retval      无
 */
void ltdc_show_char(uint16_t x, uint16_t y, char chr, uint8_t size, uint8_t mode, uint16_t color)
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
                ltdc_draw_point(x, y, color);               /* 画点出来,要显示这个点 */
            }
            else if (mode == 0)                             /* 无效点,不显示 */
            {
                ltdc_draw_point(x, y, g_back_color);        /* 画背景色,相当于这个点不显示(注意背景色由全局变量控制) */
            }

            temp <<= 1;                                     /* 移位, 以便获取下一个位的状态 */
            y++;

            if (y >= ltdcdev.height)return;                 /* 超区域了 */

            if ((y - y0) == size)                           /* 显示完一列了? */
            {
                y = y0;                                     /* y坐标复位 */
                x++;                                        /* x坐标递增 */
                
                if (x >= ltdcdev.width)
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
static uint32_t ltdc_pow(uint8_t m, uint8_t n)
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
void ltdc_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint16_t color)
{
    uint8_t t, temp;
    uint8_t enshow = 0;

    for (t = 0; t < len; t++)                                               /* 按总显示位数循环 */
    {
        temp = (num / ltdc_pow(10, len - t - 1)) % 10;                      /* 获取对应位的数字 */

        if (enshow == 0 && t < (len - 1))                                   /* 没有使能显示,且还有位要显示 */
        {
            if (temp == 0)
            {
                ltdc_show_char(x + (size / 2) * t, y, ' ', size, 0, color); /* 显示空格,占位 */
                continue;                                                   /* 继续下个一位 */
            }
            else
            {
                enshow = 1;                                                 /* 使能显示 */
            }
        }

        ltdc_show_char(x + (size / 2) * t, y, temp + '0', size, 0, color);  /* 显示字符 */
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
void ltdc_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint16_t color)
{
    uint8_t t, temp;
    uint8_t enshow = 0;

    for (t = 0; t < len; t++)                                                               /* 按总显示位数循环 */
    {
        temp = (num / ltdc_pow(10, len - t - 1)) % 10;                                      /* 获取对应位的数字 */

        if (enshow == 0 && t < (len - 1))                                                   /* 没有使能显示,且还有位要显示 */
        {
            if (temp == 0)
            {
                if (mode & 0x80)                                                            /* 高位需要填充0 */
                {
                    ltdc_show_char(x + (size / 2) * t, y, '0', size, mode & 0x01, color);   /* 用0占位 */
                }
                else
                {
                    ltdc_show_char(x + (size / 2) * t, y, ' ', size, mode & 0x01, color);   /* 用空格占位 */
                }

                continue;
            }
            else
            {
                enshow = 1;                                                                 /* 使能显示 */
            }

        }

        ltdc_show_char(x + (size / 2) * t, y, temp + '0', size, mode & 0x01, color);
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
void ltdc_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint16_t color)
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

        ltdc_show_char(x, y, *p, size, 1, color);
        x += size / 2;
        p++;
    }
}
