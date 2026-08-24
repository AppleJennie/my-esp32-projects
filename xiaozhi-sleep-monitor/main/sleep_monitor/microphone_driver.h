#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

bool microphone_init(void);
esp_err_t microphone_read(int16_t* buffer, size_t bytes_to_read, size_t* bytes_read);

#ifdef __cplusplus
}
#endif
