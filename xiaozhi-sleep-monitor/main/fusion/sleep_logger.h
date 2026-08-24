/**
 * sleep_logger.h — stubbed: SD card disabled in audio-only mode
 * All functions are no-ops or return safe defaults.
 */
#ifndef SLEEP_LOGGER_H
#define SLEEP_LOGGER_H
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void sleep_logger_notify_raw_frame(void);

#ifdef __cplusplus
}
#endif
#endif
