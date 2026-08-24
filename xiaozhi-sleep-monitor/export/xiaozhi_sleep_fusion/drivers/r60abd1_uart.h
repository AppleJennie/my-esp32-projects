#ifndef R60ABD1_UART_H
#define R60ABD1_UART_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 调试开关
 * ================================================================ */
#define R60ABD1_DEBUG_RAW_FRAME     0
#define R60ABD1_DEBUG_PARSED_DATA   0
#define R60ABD1_DEBUG_UNKNOWN_FRAME 1

/* ================================================================
 * UART 配置
 * ================================================================ */
#define R60ABD1_UART_NUM            UART_NUM_1
#define R60ABD1_UART_BAUD_RATE      115200
#define R60ABD1_UART_BUF_SIZE       (1024 * 2)
#define R60ABD1_UART_QUEUE_SIZE     20

/* ATK DNESP32S3: R60 雷达接 UART1 */
#define R60ABD1_UART_TXD_GPIO       18
#define R60ABD1_UART_RXD_GPIO       17
#define R60ABD1_UART_RTS_GPIO       (-1)
#define R60ABD1_UART_CTS_GPIO       (-1)

#define R60ABD1_INIT_DELAY_MS       30000

/* ================================================================
 * 帧格式常量
 * ================================================================ */
#define R60ABD1_FRAME_HEAD_0        0x53
#define R60ABD1_FRAME_HEAD_1        0x59
#define R60ABD1_FRAME_TAIL_0        0x54
#define R60ABD1_FRAME_TAIL_1        0x43

#define R60ABD1_MAX_PAYLOAD_LEN     128

typedef struct {
    uint8_t  head[2];
    uint8_t  control;
    uint8_t  command;
    uint16_t data_len;
    uint8_t  payload[R60ABD1_MAX_PAYLOAD_LEN];
    uint8_t  checksum;
    uint8_t  tail[2];
} r60abd1_frame_t;

/* ================================================================
 * control + command 宏
 * ================================================================ */

/* 系统 */
#define R60_CTRL_SYSTEM             0x01
#define R60_CMD_HEARTBEAT           0x01

/* 人体存在 */
#define R60_CTRL_PRESENCE           0x80
#define R60_CMD_SWITCH_REPLY        0x00  /* 功能开关回复 */
#define R60_CMD_PRESENCE_STATE      0x01
#define R60_CMD_MOTION_STATE        0x02
#define R60_CMD_BODY_MOTION         0x03
#define R60_CMD_DISTANCE            0x04
#define R60_CMD_TARGET_3D           0x05
#define R60_CMD_PRESENCE_QUERY      0x81  /* 有人/无人查询回复 */

/* 呼吸 */
#define R60_CTRL_BREATH             0x81
#define R60_CMD_BREATH_STATUS       0x01
#define R60_CMD_BREATH_RATE         0x02
#define R60_CMD_BREATH_WAVE_REPORT  0x05
#define R60_CMD_BREATH_QUERY        0x81  /* 呼吸状态查询回复 */
#define R60_CMD_BREATH_WAVE_QUERY   0x85

/* 睡眠（只保留入床/离床查询） */
#define R60_CTRL_SLEEP              0x84
#define R60_CMD_IN_BED              0x01
#define R60_CMD_IN_BED_QUERY        0x81  /* 入床/离床查询回复 */

/* 心率 */
#define R60_CTRL_HEART              0x85
#define R60_CMD_HEART_RATE          0x02
#define R60_CMD_HEART_WAVE_REPORT   0x05
#define R60_CMD_HEART_WAVE_QUERY    0x85

/* ================================================================
 * 初始化命令（开机 30 秒后发送，3 条）
 * ================================================================ */
/* 打开人体存在: 53 59 80 00 00 01 01 2E 54 43 */
#define R60_INIT_PRESENCE  {0x53,0x59,0x80,0x00,0x00,0x01,0x01,0x2E,0x54,0x43}

/* 打开呼吸监测: 53 59 81 00 00 01 01 2F 54 43 */
#define R60_INIT_BREATH    {0x53,0x59,0x81,0x00,0x00,0x01,0x01,0x2F,0x54,0x43}

/* 打开心率监测: 53 59 85 00 00 01 01 33 54 43 */
#define R60_INIT_HEART     {0x53,0x59,0x85,0x00,0x00,0x01,0x01,0x33,0x54,0x43}

/* ================================================================
 * 定时查询命令
 * ================================================================ */
/* 呼吸波形: 53 59 81 85 00 01 0F C2 54 43 */
#define R60_QUERY_BREATH_WAVE  {0x53,0x59,0x81,0x85,0x00,0x01,0x0F,0xC2,0x54,0x43}

/* 心率波形: 53 59 85 85 00 01 0F C6 54 43 */
#define R60_QUERY_HEART_WAVE   {0x53,0x59,0x85,0x85,0x00,0x01,0x0F,0xC6,0x54,0x43}

/* 有人/无人: 53 59 80 81 00 01 0F BD 54 43 */
#define R60_QUERY_PRESENCE     {0x53,0x59,0x80,0x81,0x00,0x01,0x0F,0xBD,0x54,0x43}

/* 呼吸状态: 53 59 81 81 00 01 0F BE 54 43 */
#define R60_QUERY_BREATH_STAT  {0x53,0x59,0x81,0x81,0x00,0x01,0x0F,0xBE,0x54,0x43}

/* 入床/离床: 53 59 84 81 00 01 0F C1 54 43 */
#define R60_QUERY_IN_BED       {0x53,0x59,0x84,0x81,0x00,0x01,0x0F,0xC1,0x54,0x43}

/* ================================================================
 * 枚举类型
 * ================================================================ */
typedef enum {
    R60_PRESENCE_NONE    = 0,
    R60_PRESENCE_SOMEONE = 1,
    R60_PRESENCE_UNKNOWN = 0xFF,
} r60_presence_t;

typedef enum {
    R60_MOTION_UNKNOWN = 0,
    R60_MOTION_STILL   = 1,
    R60_MOTION_ACTIVE  = 2,
} r60_motion_state_t;

typedef enum {
    R60_BREATH_NONE     = 0,
    R60_BREATH_NORMAL   = 1,
    R60_BREATH_TOO_LOW  = 2,
    R60_BREATH_TOO_HIGH = 3,
    R60_BREATH_UNKNOWN  = 0xFF,
} r60_breath_status_t;

typedef enum {
    R60_BED_OFF     = 0,
    R60_BED_ON      = 1,
    R60_BED_UNKNOWN = 0xFF,
} r60_bed_status_t;

/* ================================================================
 * API
 * ================================================================ */
esp_err_t r60abd1_uart_init(void);
esp_err_t r60abd1_uart_start_task(void);
void      r60abd1_uart_stop_task(void);
int       r60abd1_uart_send(const uint8_t *data, uint16_t len);
void      r60abd1_send_init_commands(void);
void      r60abd1_handle_frame(const r60abd1_frame_t *frame);
void      r60abd1_debug_print_raw(const uint8_t *buf, uint16_t len);
void      r60abd1_query_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif
