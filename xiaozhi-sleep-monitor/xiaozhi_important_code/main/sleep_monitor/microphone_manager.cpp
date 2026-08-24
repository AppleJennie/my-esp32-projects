#include "microphone_manager.h"
#include "microphone_driver.h"
#include "app_config.h"
#include <esp_log.h>

static const char* TAG = "MIC_MGR";
static bool s_mic_initialized = false;

bool microphone_manager_init(void) {
    if (s_mic_initialized) {
        return true;
    }

    if (!microphone_init()) {
        ESP_LOGE(TAG, "Failed to init microphone");
        return false;
    }

    s_mic_initialized = true;
    ESP_LOGI(TAG, "Microphone manager ready (simulate mode)");
    return true;
}

void microphone_manager_deinit(void) {
    if (!s_mic_initialized) {
        return;
    }
    s_mic_initialized = false;
    ESP_LOGI(TAG, "Microphone manager deinitialized");
}

bool microphone_manager_is_initialized(void) {
    return s_mic_initialized;
}
