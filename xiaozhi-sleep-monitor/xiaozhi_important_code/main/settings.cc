#include "settings.h"

#include <esp_log.h>
#include <nvs_flash.h>

#define TAG "Settings"

Settings::Settings(const std::string& ns, bool read_write) : ns_(ns), read_write_(read_write) {
    esp_err_t err = nvs_open(ns.c_str(), read_write_ ? NVS_READWRITE : NVS_READONLY, &nvs_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open %s failed: %s", ns.c_str(), esp_err_to_name(err));
        nvs_handle_ = 0;
    }
}

Settings::~Settings() {
    if (nvs_handle_ != 0) {
        if (read_write_ && dirty_) {
            esp_err_t err = nvs_commit(nvs_handle_);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
            }
        }
        nvs_close(nvs_handle_);
    }
}

std::string Settings::GetString(const std::string& key, const std::string& default_value) {
    if (nvs_handle_ == 0) {
        return default_value;
    }

    size_t length = 0;
    if (nvs_get_str(nvs_handle_, key.c_str(), nullptr, &length) != ESP_OK) {
        return default_value;
    }

    std::string value;
    value.resize(length);
    esp_err_t err = nvs_get_str(nvs_handle_, key.c_str(), value.data(), &length);
    if (err != ESP_OK) {
        return default_value;
    }
    while (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
    return value;
}

void Settings::SetString(const std::string& key, const std::string& value) {
    if (!read_write_ || nvs_handle_ == 0) {
        ESP_LOGW(TAG, "Namespace %s not open for writing", ns_.c_str());
        return;
    }
    esp_err_t err = nvs_set_str(nvs_handle_, key.c_str(), value.c_str());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str %s failed: %s", key.c_str(), esp_err_to_name(err));
        return;
    }
    err = nvs_commit(nvs_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
        return;
    }
    dirty_ = true;
}

int32_t Settings::GetInt(const std::string& key, int32_t default_value) {
    if (nvs_handle_ == 0) {
        return default_value;
    }

    int32_t value;
    if (nvs_get_i32(nvs_handle_, key.c_str(), &value) != ESP_OK) {
        return default_value;
    }
    return value;
}

void Settings::SetInt(const std::string& key, int32_t value) {
    if (!read_write_ || nvs_handle_ == 0) {
        ESP_LOGW(TAG, "Namespace %s not open for writing", ns_.c_str());
        return;
    }
    esp_err_t err = nvs_set_i32(nvs_handle_, key.c_str(), value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_i32 %s failed: %s", key.c_str(), esp_err_to_name(err));
        return;
    }
    err = nvs_commit(nvs_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
        return;
    }
    dirty_ = true;
}

bool Settings::GetBool(const std::string& key, bool default_value) {
    if (nvs_handle_ == 0) {
        return default_value;
    }

    uint8_t value;
    if (nvs_get_u8(nvs_handle_, key.c_str(), &value) != ESP_OK) {
        return default_value;
    }
    return value != 0;
}

void Settings::SetBool(const std::string& key, bool value) {
    if (!read_write_ || nvs_handle_ == 0) {
        ESP_LOGW(TAG, "Namespace %s not open for writing", ns_.c_str());
        return;
    }
    esp_err_t err = nvs_set_u8(nvs_handle_, key.c_str(), value ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_u8 %s failed: %s", key.c_str(), esp_err_to_name(err));
        return;
    }
    err = nvs_commit(nvs_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
        return;
    }
    dirty_ = true;
}

void Settings::EraseKey(const std::string& key) {
    if (!read_write_ || nvs_handle_ == 0) {
        ESP_LOGW(TAG, "Namespace %s not open for writing", ns_.c_str());
        return;
    }
    auto ret = nvs_erase_key(nvs_handle_, key.c_str());
    if (ret != ESP_ERR_NVS_NOT_FOUND && ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_erase_key %s failed: %s", key.c_str(), esp_err_to_name(ret));
    }
}

void Settings::EraseAll() {
    if (!read_write_ || nvs_handle_ == 0) {
        ESP_LOGW(TAG, "Namespace %s not open for writing", ns_.c_str());
        return;
    }
    auto ret = nvs_erase_all(nvs_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_erase_all failed: %s", esp_err_to_name(ret));
    }
}
