#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// 初始化 SD 卡日志模块（非阻塞，失败不影响系统运行）
bool sleep_sd_logger_init(void);

// 反初始化
void sleep_sd_logger_deinit(void);

// 开启一个 session 目录：/sdcard/sleep/YYYY-MM-DD/session_HH-MM-SS/
bool sleep_sd_logger_open_session(const char* date_str, const char* session_time_str);

// 关闭当前 session
void sleep_sd_logger_close_session(void);

// 写入结构化实时数据到 realtime.csv
void sleep_sd_logger_write_realtime(uint32_t ts_ms, int hr, int br, float spo2,
                                    int body_present, int motion,
                                    float temp, float humi, int light);

// 写入鼾声事件到 snore_events.csv
void sleep_sd_logger_write_snore(uint32_t ts_ms, uint32_t duration_ms,
                                 float snore_prob, float rms_db);

// 写入报告文件
void sleep_sd_logger_write_report_json(const char* json_str);
void sleep_sd_logger_write_report_txt(const char* txt_str);

// 兼容旧接口：写入原始 JSON 行到 raw.jsonl
void sleep_sd_logger_write(const char* json_line);

// 查询 SD 是否可用
bool sleep_sd_logger_is_available(void);

#ifdef __cplusplus
}
#endif
