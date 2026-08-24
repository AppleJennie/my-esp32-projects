#include "sleep_sd_logger.h"
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdspi_host.h>
#include <driver/gpio.h>
#include <driver/spi_common.h>
#include <esp_timer.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_heap_caps.h>

static const char* TAG = "SD_LOG";

// ============================================================================
// DNESP32S3 硬件引脚定义（官方手册：IO11=MOSI, IO12=SCK, IO13=MISO, IO2=TF_CS）
// ============================================================================
#define SD_HOST         SPI3_HOST
#define SD_PIN_CS       GPIO_NUM_2
#define SD_PIN_SCK      GPIO_NUM_12
#define SD_PIN_MOSI     GPIO_NUM_11
#define SD_PIN_MISO     GPIO_NUM_13
#define SD_MAX_FREQ_KHZ 10000
#define SD_MOUNT_POINT  "/sdcard"

// 编译期防呆：引脚必须与 DNESP32S3 官方硬件一致
#if SD_PIN_SCK != GPIO_NUM_12
#error "DNESP32S3 SD SCK must be GPIO12"
#endif
#if SD_PIN_MOSI != GPIO_NUM_11
#error "DNESP32S3 SD MOSI must be GPIO11"
#endif
#if SD_PIN_MISO != GPIO_NUM_13
#error "DNESP32S3 SD MISO must be GPIO13"
#endif
#if SD_PIN_CS != GPIO_NUM_2
#error "DNESP32S3 SD CS must be GPIO2"
#endif

static bool s_sd_available = false;
static sdmmc_card_t* s_card = NULL;

// Session paths
static char s_session_dir[128]  = {0};
static char s_realtime_path[160]  = {0};
static char s_snore_path[160]     = {0};
static char s_rawjsonl_path[160]  = {0};
static char s_report_json_path[160] = {0};
static char s_report_txt_path[160]  = {0};
static bool s_session_active = false;

// ============================================================================
// SD Writer Task：队列缓冲，低优先级，避免在高优先级任务里直接 fopen/fwrite
// ============================================================================
#define SD_WRITER_QUEUE_LEN 32

typedef enum {
    SD_WRITE_REALTIME,
    SD_WRITE_SNORE,
    SD_WRITE_RAW_JSONL,
    SD_WRITE_REPORT_JSON,
    SD_WRITE_REPORT_TXT,
} SdWriteType;

typedef struct {
    SdWriteType type;
    char* text;   // heap allocated for text writes; NULL for realtime/snore
    uint32_t ts_ms;
    int hr, br;
    float spo2;
    int body_present, motion;
    float temp, humi;
    int light;
    uint32_t duration_ms;
    float snore_prob, rms_db;
} SdWriterItem;

static QueueHandle_t s_writer_queue = NULL;
static TaskHandle_t  s_writer_task_handle = NULL;

static void sd_writer_task(void* pvParameters) {
    (void)pvParameters;
    ESP_LOGI(TAG, "sd_writer_task started core=%d prio=%d",
             xPortGetCoreID(), uxTaskPriorityGet(NULL));

    SdWriterItem item;
    uint32_t last_stat_ms = 0;
    uint32_t write_count = 0;

    while (true) {
        if (xQueueReceive(s_writer_queue, &item, pdMS_TO_TICKS(1000)) == pdPASS) {
            if (!s_sd_available || !s_session_active) {
                if (item.text) heap_caps_free(item.text);
                continue;
            }

            switch (item.type) {
            case SD_WRITE_REALTIME: {
                if (s_realtime_path[0]) {
                    FILE* f = fopen(s_realtime_path, "a");
                    if (f) {
                        fprintf(f, "%lu,%d,%d,%.1f,%d,%d,%.1f,%.1f,%d\n",
                                (unsigned long)item.ts_ms, item.hr, item.br,
                                item.spo2, item.body_present, item.motion,
                                item.temp, item.humi, item.light);
                        fclose(f);
                        write_count++;
                    }
                }
                break;
            }
            case SD_WRITE_SNORE: {
                if (s_snore_path[0]) {
                    FILE* f = fopen(s_snore_path, "a");
                    if (f) {
                        fprintf(f, "%lu,%lu,%.3f,%.1f\n",
                                (unsigned long)item.ts_ms,
                                (unsigned long)item.duration_ms,
                                item.snore_prob, item.rms_db);
                        fclose(f);
                        write_count++;
                    }
                }
                break;
            }
            case SD_WRITE_RAW_JSONL: {
                if (s_rawjsonl_path[0] && item.text) {
                    FILE* f = fopen(s_rawjsonl_path, "a");
                    if (f) {
                        uint32_t sec = (uint32_t)(esp_timer_get_time() / 1000000ULL);
                        fprintf(f, "[%lu] %s\n", (unsigned long)sec, item.text);
                        fclose(f);
                        write_count++;
                    }
                    heap_caps_free(item.text);
                }
                break;
            }
            case SD_WRITE_REPORT_JSON: {
                if (s_report_json_path[0] && item.text) {
                    FILE* f = fopen(s_report_json_path, "w");
                    if (f) {
                        fprintf(f, "%s\n", item.text);
                        fclose(f);
                        ESP_LOGI(TAG, "Report JSON saved: %s", s_report_json_path);
                    }
                    heap_caps_free(item.text);
                }
                break;
            }
            case SD_WRITE_REPORT_TXT: {
                if (s_report_txt_path[0] && item.text) {
                    FILE* f = fopen(s_report_txt_path, "w");
                    if (f) {
                        fprintf(f, "%s\n", item.text);
                        fclose(f);
                        ESP_LOGI(TAG, "Report TXT saved: %s", s_report_txt_path);
                    }
                    heap_caps_free(item.text);
                }
                break;
            }
            }
        }

        // 每 30 秒打印一次系统内存和栈监控
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (now_ms - last_stat_ms >= 30000) {
            last_stat_ms = now_ms;
            ESP_LOGI(TAG, "SYSSTAT SRAM free=%d min_free=%d PSRAM free=%d | sd_writer stack_hwm=%d writes=%lu",
                     heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                     heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                     uxTaskGetStackHighWaterMark(NULL),
                     (unsigned long)write_count);
            write_count = 0;
        }
    }
}

static bool sd_writer_enqueue(const SdWriterItem* item) {
    if (!s_writer_queue) return false;
    if (xQueueSend(s_writer_queue, item, 0) != pdPASS) {
        if (item->text) heap_caps_free(item->text);
        ESP_LOGW(TAG, "SD writer queue full, drop type=%d", item->type);
        return false;
    }
    return true;
}

// ============================================================================
// Public API
// ============================================================================

bool sleep_sd_logger_is_available(void) {
    return s_sd_available;
}

bool sleep_sd_logger_init(void) {
    // 1. 创建 writer 队列和任务（先创建，这样即使 SD 挂载失败也能后续恢复）
    if (!s_writer_queue) {
        s_writer_queue = xQueueCreate(SD_WRITER_QUEUE_LEN, sizeof(SdWriterItem));
        if (!s_writer_queue) {
            ESP_LOGE(TAG, "Writer queue create failed");
            return false;
        }
    }
    if (!s_writer_task_handle) {
        BaseType_t r = xTaskCreatePinnedToCore(
            sd_writer_task, "sd_writer", 8192, NULL, 2, &s_writer_task_handle, 0);
        if (r != pdPASS) {
            ESP_LOGE(TAG, "sd_writer_task create failed");
            vQueueDelete(s_writer_queue);
            s_writer_queue = NULL;
            return false;
        }
    }

    // 2. 初始化 SPI3 总线（独立，不复用 LCD 的 SPI2）
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = SD_PIN_MOSI;
    buscfg.miso_io_num = SD_PIN_MISO;
    buscfg.sclk_io_num = SD_PIN_SCK;
    buscfg.quadwp_io_num = GPIO_NUM_NC;
    buscfg.quadhd_io_num = GPIO_NUM_NC;
    buscfg.max_transfer_sz = 4096;

    esp_err_t spi_err = spi_bus_initialize(SD_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (spi_err != ESP_OK) {
        ESP_LOGE(TAG, "SPI3 bus init failed: %s", esp_err_to_name(spi_err));
        return false;
    }

    // 3. SDSPI 配置
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_HOST;
    host.max_freq_khz = SD_MAX_FREQ_KHZ;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_PIN_CS;
    slot_config.host_id = SD_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
    };

    ESP_LOGI(TAG, "SD mode: SDSPI, HOST=SPI3, CS=%d SCK=%d MOSI=%d MISO=%d freq=%dkHz",
             SD_PIN_CS, SD_PIN_SCK, SD_PIN_MOSI, SD_PIN_MISO, SD_MAX_FREQ_KHZ);
    ESP_LOGI(TAG, "SD mount_point: %s", SD_MOUNT_POINT);

    esp_err_t ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD card mount failed: %s", esp_err_to_name(ret));
        s_sd_available = false;
        return false;
    }

    ESP_LOGI(TAG, "SD card mounted OK, size: %llu MB",
             ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size / (1024 * 1024));
    s_sd_available = true;
    return true;
}

void sleep_sd_logger_deinit(void) {
    if (s_card) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
        s_card = NULL;
    }
    s_sd_available = false;
}

static void ensure_dir(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        if (mkdir(path, 0755) != 0) {
            ESP_LOGW(TAG, "mkdir %s failed", path);
        }
    }
}

bool sleep_sd_logger_open_session(const char* date_str, const char* session_time_str) {
    if (!s_sd_available) {
        return false;
    }
    sleep_sd_logger_close_session();

    snprintf(s_session_dir, sizeof(s_session_dir),
             "%s/sleep/%s/session_%s", SD_MOUNT_POINT, date_str, session_time_str);

    ensure_dir(SD_MOUNT_POINT "/sleep");
    char day_dir[96];
    snprintf(day_dir, sizeof(day_dir), "%s/sleep/%s", SD_MOUNT_POINT, date_str);
    ensure_dir(day_dir);
    ensure_dir(s_session_dir);

    snprintf(s_realtime_path, sizeof(s_realtime_path), "%s/realtime.csv", s_session_dir);
    snprintf(s_snore_path, sizeof(s_snore_path), "%s/snore_events.csv", s_session_dir);
    snprintf(s_rawjsonl_path, sizeof(s_rawjsonl_path), "%s/raw.jsonl", s_session_dir);
    snprintf(s_report_json_path, sizeof(s_report_json_path), "%s/report.json", s_session_dir);
    snprintf(s_report_txt_path, sizeof(s_report_txt_path), "%s/report.txt", s_session_dir);

    // 写入 CSV 表头
    FILE* f1 = fopen(s_realtime_path, "w");
    if (f1) {
        fprintf(f1, "timestamp_ms,heart_rate,breath_rate,spo2,body_present,motion_level,temp,humi,light\n");
        fclose(f1);
    }
    FILE* f2 = fopen(s_snore_path, "w");
    if (f2) {
        fprintf(f2, "timestamp_ms,duration_ms,snore_prob,rms_db\n");
        fclose(f2);
    }

    s_session_active = true;
    ESP_LOGI(TAG, "Session opened: %s", s_session_dir);
    return true;
}

void sleep_sd_logger_close_session(void) {
    s_session_active = false;
    s_session_dir[0]    = '\0';
    s_realtime_path[0]  = '\0';
    s_snore_path[0]     = '\0';
    s_rawjsonl_path[0]  = '\0';
    s_report_json_path[0] = '\0';
    s_report_txt_path[0]  = '\0';
}

void sleep_sd_logger_write_realtime(uint32_t ts_ms, int hr, int br, float spo2,
                                    int body_present, int motion,
                                    float temp, float humi, int light) {
    if (!s_sd_available || !s_session_active) {
        return;
    }
    SdWriterItem item = {};
    item.type = SD_WRITE_REALTIME;
    item.ts_ms = ts_ms;
    item.hr = hr;
    item.br = br;
    item.spo2 = spo2;
    item.body_present = body_present;
    item.motion = motion;
    item.temp = temp;
    item.humi = humi;
    item.light = light;
    sd_writer_enqueue(&item);
}

void sleep_sd_logger_write_snore(uint32_t ts_ms, uint32_t duration_ms,
                                 float snore_prob, float rms_db) {
    if (!s_sd_available || !s_session_active) {
        return;
    }
    SdWriterItem item = {};
    item.type = SD_WRITE_SNORE;
    item.ts_ms = ts_ms;
    item.duration_ms = duration_ms;
    item.snore_prob = snore_prob;
    item.rms_db = rms_db;
    sd_writer_enqueue(&item);
}

static void enqueue_text(SdWriteType type, const char* text) {
    if (!text) return;
    size_t len = strlen(text) + 1;
    char* copy = (char*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy) {
        copy = (char*)heap_caps_malloc(len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!copy) {
        ESP_LOGE(TAG, "text malloc failed %u bytes", len);
        return;
    }
    memcpy(copy, text, len);
    SdWriterItem item = {};
    item.type = type;
    item.text = copy;
    sd_writer_enqueue(&item);
}

void sleep_sd_logger_write_report_json(const char* json_str) {
    if (!s_sd_available || !json_str) return;
    enqueue_text(SD_WRITE_REPORT_JSON, json_str);
}

void sleep_sd_logger_write_report_txt(const char* txt_str) {
    if (!s_sd_available || !txt_str) return;
    enqueue_text(SD_WRITE_REPORT_TXT, txt_str);
}

void sleep_sd_logger_write(const char* json_line) {
    if (!s_sd_available || !json_line) {
        return;
    }
    if (s_session_active && s_rawjsonl_path[0] != '\0') {
        enqueue_text(SD_WRITE_RAW_JSONL, json_line);
    }
}
