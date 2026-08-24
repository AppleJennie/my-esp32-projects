#ifndef HARDWARE_CONTROL_H
#define HARDWARE_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HW_CMD_MONITOR_START = 0,
    HW_CMD_MONITOR_STOP,
    HW_CMD_DND_ON,
    HW_CMD_DND_OFF,
    HW_CMD_WIFI_ON,
    HW_CMD_WIFI_OFF,
    HW_CMD_SET_BRIGHTNESS,
    HW_CMD_SET_VOLUME,
    HW_CMD_LCD_SCREEN_OFF,
    HW_CMD_LCD_SCREEN_ON,
    HW_CMD_RADAR_RESET,
    HW_CMD_RADAR_ENABLE,
    HW_CMD_RADAR_DISABLE,
    HW_CMD_MIC_ENABLE,
    HW_CMD_MIC_DISABLE,
    HW_CMD_ALARM_TEST,
    HW_CMD_EXPORT_DATA,
    HW_CMD_CLEAR_HISTORY,
    HW_CMD_NIGHT_MODE_ON,
    HW_CMD_NIGHT_MODE_OFF,
    HW_CMD_KEEP_SCREEN_ON,
    HW_CMD_KEEP_SCREEN_OFF,
    HW_CMD_SYNC_TIME,
    HW_CMD_RADAR_CALIBRATE,
    HW_CMD_MIC_TEST,
    HW_CMD_FACTORY_RESET,
} hardware_cmd_id_t;

/* 硬件控制消息结构 */
typedef struct {
    hardware_cmd_id_t cmd;
    int value;
} hardware_cmd_t;

/**
 * @brief 初始化硬件控制模块（创建 FreeRTOS 队列和任务）
 */
void hardware_control_init(void);

/**
 * @brief 发送硬件控制命令（非阻塞，从 LVGL 事件回调中调用）
 * @param cmd 命令 ID
 * @param value 附加值（如亮度值、音量值等）
 * @return true 发送成功，false 队列满
 */
bool hardware_control_send(hardware_cmd_id_t cmd, int value);

#ifdef __cplusplus
}
#endif

#endif
