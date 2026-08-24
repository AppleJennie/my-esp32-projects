/**
 * sleep_logger.c — stubbed: SD disabled, no file I/O
 */
#include "sleep_logger.h"

static uint32_t s_raw_count = 0;

void sleep_logger_notify_raw_frame(void)
{
    s_raw_count++;
}
