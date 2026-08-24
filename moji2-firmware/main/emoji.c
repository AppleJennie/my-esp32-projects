#include "emoji.h"

#include <math.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "bsp_display.h"
#include "board.h"

#define BAND_LINES      72          /* 360 / 72 = 5 个行带 */

/* RGB565 打包 */
#define RGB565(r, g, b)  (uint16_t)((((uint16_t)(r) & 0xF8) << 8) | \
                                    (((uint16_t)(g) & 0xFC) << 3) | \
                                    ((uint16_t)(b) >> 3))
/* SPI 面板要求高字节先发送，而 ESP32 为小端存储，故交换高低字节 */
#define PX(r, g, b)      (uint16_t)((RGB565((r), (g), (b)) >> 8) | (RGB565((r), (g), (b)) << 8))

#define COL_BG          PX(16, 18, 28)      /* 深色背景 */
#define COL_FACE        PX(245, 245, 245)   /* 五官白色 */
#define COL_BLUSH       PX(235, 110, 110)   /* 腮红 */
#define COL_BATT_EDGE   PX(90, 92, 100)     /* 电量框 */
#define COL_BATT_HI     PX(70, 215, 90)
#define COL_BATT_MID    PX(230, 190, 40)
#define COL_BATT_LO     PX(230, 60, 50)

/* 面部布局（圆屏 360x360，中心 180,180） */
#define EYE_LX      110
#define EYE_RX      250
#define EYE_CY      150
#define EYE_RADX    38
#define EYE_RADY    55
#define MOUTH_CX    180

/* 电量图标（顶部中央） */
#define BATT_X0     150
#define BATT_X1     206
#define BATT_Y0     34
#define BATT_Y1     48

static uint16_t *s_band = NULL;     /* LCD_H_RES x BAND_LINES */

static inline void span(uint16_t *row, int x0, int x1, uint16_t color)
{
    if (x0 < 0) x0 = 0;
    if (x1 > LCD_H_RES - 1) x1 = LCD_H_RES - 1;
    for (int x = x0; x <= x1; x++) {
        row[x] = color;
    }
}

/* 在当前行绘制竖直中心为 cy 的实心椭圆的该行部分 */
static void ellipse_span(uint16_t *row, int y, int cx, int cy, int rx, int ry, uint16_t color)
{
    if (ry <= 0) return;
    int dy = y - cy;
    if (dy < -ry || dy > ry) return;
    float t = 1.0f - (float)(dy * dy) / (float)(ry * ry);
    int hw = (int)(rx * sqrtf(t));
    span(row, cx - hw, cx + hw, color);
}

/* 斜粗线（眉毛）：从 (x0,y0) 到 (x1,y1)，半厚 half_th */
static void thick_line_span(uint16_t *row, int y, int x0, int y0, int x1, int y1,
                            int half_th, uint16_t color)
{
    int top = y0 < y1 ? y0 : y1;
    int bot = y0 > y1 ? y0 : y1;
    if (y < top || y > bot || bot == top) return;
    int x = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
    span(row, x - half_th, x + half_th, color);
}

static void draw_mouth(uint16_t *row, int y, emoji_expr_t expr)
{
    switch (expr) {
    case EMOJI_HAPPY:
        /* “D” 形大笑：只画椭圆下半部分 */
        if (y >= 222) {
            ellipse_span(row, y, MOUTH_CX, 222, 44, 30, COL_FACE);
        }
        break;
    case EMOJI_ANGRY:
        ellipse_span(row, y, MOUTH_CX, 248, 30, 7, COL_FACE);
        break;
    case EMOJI_SLEEPY:
        ellipse_span(row, y, MOUTH_CX, 240, 13, 13, COL_FACE);
        break;
    case EMOJI_LISTENING:
        ellipse_span(row, y, MOUTH_CX, 240, 15, 12, COL_FACE);
        break;
    case EMOJI_NEUTRAL:
    default:
        ellipse_span(row, y, MOUTH_CX, 238, 26, 10, COL_FACE);
        break;
    }
}

static void draw_battery(uint16_t *row, int y, int pct)
{
    if (pct < 0 || y < BATT_Y0 || y > BATT_Y1) return;

    /* 边框（上下横线 + 两侧竖线 + 右侧凸台） */
    if (y == BATT_Y0 || y == BATT_Y1) {
        span(row, BATT_X0, BATT_X1, COL_BATT_EDGE);
    } else {
        row[BATT_X0] = COL_BATT_EDGE;
        row[BATT_X1] = COL_BATT_EDGE;
        if (y > BATT_Y0 + 2 && y < BATT_Y1 - 2) {
            span(row, BATT_X1 + 1, BATT_X1 + 3, COL_BATT_EDGE);   /* 凸台 */
        }
        /* 电量填充 */
        int inner = BATT_X1 - BATT_X0 - 4;      /* 内腔宽度 */
        int fill = inner * (pct > 100 ? 100 : pct) / 100;
        if (fill > 0 && y > BATT_Y0 + 1 && y < BATT_Y1 - 1) {
            uint16_t c = pct > 60 ? COL_BATT_HI : (pct > 30 ? COL_BATT_MID : COL_BATT_LO);
            span(row, BATT_X0 + 2, BATT_X0 + 1 + fill, c);
        }
    }
}

static void render_row(int y, uint16_t *row, const emoji_scene_t *sc)
{
    for (int x = 0; x < LCD_H_RES; x++) {
        row[x] = COL_BG;
    }

    /* 眼睛：眨眼通过压缩 ry 实现 */
    float scale = sc->eye_open;
    if (sc->expr == EMOJI_LISTENING) scale *= 1.2f;
    if (sc->expr == EMOJI_SLEEPY)    scale *= 0.15f;
    int ry = (int)(EYE_RADY * scale);
    if (ry < 3) ry = 3;
    ellipse_span(row, y, EYE_LX, EYE_CY, EYE_RADX, ry, COL_FACE);
    ellipse_span(row, y, EYE_RX, EYE_CY, EYE_RADX, ry, COL_FACE);

    /* 生气：加倒八字眉 */
    if (sc->expr == EMOJI_ANGRY) {
        thick_line_span(row, y, 82, 100, 140, 124, 7, COL_FACE);
        thick_line_span(row, y, 278, 100, 220, 124, 7, COL_FACE);
    }

    /* 开心：加腮红 */
    if (sc->expr == EMOJI_HAPPY) {
        ellipse_span(row, y, 78, 205, 20, 10, COL_BLUSH);
        ellipse_span(row, y, 282, 205, 20, 10, COL_BLUSH);
    }

    draw_mouth(row, y, sc->expr);
    draw_battery(row, y, sc->battery_pct);
}

esp_err_t emoji_init(void)
{
    if (s_band) return ESP_OK;
    s_band = heap_caps_malloc(LCD_H_RES * BAND_LINES * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!s_band) {
        ESP_LOGE("emoji", "行带缓冲区分配失败");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void emoji_draw(const emoji_scene_t *sc)
{
    for (int y0 = 0; y0 < LCD_V_RES; y0 += BAND_LINES) {
        for (int l = 0; l < BAND_LINES; l++) {
            render_row(y0 + l, s_band + l * LCD_H_RES, sc);
        }
        bsp_display_flush(s_band, y0, y0 + BAND_LINES);
    }
}
