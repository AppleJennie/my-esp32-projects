#include "wifi_board.h"
#include "codecs/es8388_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "i2c_device.h"
#include "led/single_led.h"
#include "esp_video.h"
#include "mcp_server.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>

// Fusion task API (R60 radar + sleep fusion engine)
extern "C" {
bool fusion_task_init(void);
bool fusion_task_start(void);
void fusion_task_stop(void);
}

#include "sleep_data_center.h"

#define TAG "atk_dnesp32s3"

class XL9555 : public I2cDevice {
public:
    XL9555(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        WriteReg(0x06, 0x03);
        WriteReg(0x07, 0xF0);
    }

    void SetOutputState(uint8_t bit, uint8_t level) {
        uint16_t data;
        int index = bit;

        if (bit < 8) {
            data = ReadReg(0x02);
        } else {
            data = ReadReg(0x03);
            index -= 8;
        }

        data = (data & ~(1 << index)) | (level << index);

        if (bit < 8) {
            WriteReg(0x02, data);
        } else {
            WriteReg(0x03, data);
        }
    }
};

class atk_dnesp32s3 : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
public:
    i2c_master_bus_handle_t GetI2cBus() { return i2c_bus_; }
    Button boot_button_;
    LcdDisplay* display_;
    XL9555* xl9555_;
    EspVideo* camera_ = nullptr;

    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));

        // Initialize XL9555
        xl9555_ = new XL9555(i2c_bus_, 0x20);
    }

    // Initialize spi peripheral
    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = LCD_MOSI_PIN;   // GPIO45
        buscfg.miso_io_num = GPIO_NUM_NC;    // LCD 为单向写，不需要 MISO
        buscfg.sclk_io_num = LCD_SCLK_PIN;   // GPIO5
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    void InitializeTools() {
        auto& mcp = McpServer::GetInstance();

        /* ── 控制类工具 ── */
        mcp.AddTool(
            "self.sleep.start",
            "开始睡眠监测会话，重置统计数据。已在监测中时不会重复开启。",
            {},
            [](const PropertyList&) -> ReturnValue {
                SleepDataCenter::GetInstance().StartSession();
                return ReturnValue("{\"success\":true,\"message\":\"已开始睡眠监测\"}");
            }
        );

        mcp.AddTool(
            "self.sleep.stop",
            "停止睡眠监测并生成报告写入 SD 卡。不会影响融合引擎后台运行。",
            {},
            [](const PropertyList&) -> ReturnValue {
                SleepDataCenter::GetInstance().StopSession();
                return ReturnValue("{\"success\":true,\"message\":\"已停止睡眠监测，报告已生成\"}");
            }
        );

        /* ── 查询类工具：AI 可自由调用 ── */
        mcp.AddTool(
            "self.sleep.status",
            "查询当前睡眠监测状态（是否在运行、已运行时长、最新鼾声概率、基线状态）。只基于返回 JSON 回答，不要编造数据。",
            {},
            [](const PropertyList&) -> ReturnValue {
                return ReturnValue(SleepDataCenter::GetInstance().GetStatusJson());
            }
        );

        mcp.AddTool(
            "self.sleep.get_vitals",
            "查询当前生命体征（心率、呼吸率、血氧的当前值和平均值）。只基于返回 JSON 回答，不要编造数据。如果没有数据，直接告诉用户数据不足。",
            {},
            [](const PropertyList&) -> ReturnValue {
                return ReturnValue(SleepDataCenter::GetInstance().GetVitalsJson());
            }
        );

        mcp.AddTool(
            "self.sleep.get_snore_status",
            "查询鼾声状态（次数、时长、等级）。只基于返回 JSON 回答，不要编造数据。如果没有检测到鼾声，只能说'本次监测未检测到明显疑似鼾声'。",
            {},
            [](const PropertyList&) -> ReturnValue {
                return ReturnValue(SleepDataCenter::GetInstance().GetSnoreStatusJson());
            }
        );

        mcp.AddTool(
            "self.sleep.get_environment",
            "查询睡眠环境（温度、湿度、光照、舒适度评价、改善建议）。只基于返回 JSON 回答，不要编造数据。",
            {},
            [](const PropertyList&) -> ReturnValue {
                return ReturnValue(SleepDataCenter::GetInstance().GetEnvironmentJson());
            }
        );

        mcp.AddTool(
            "self.sleep.get_apnea_risk",
            "查询呼吸暂停专项风险评估（rREI、低氧负荷、心率代偿、风险等级、亚型分类）。只基于返回 JSON 回答，不要编造数据，不要给出医学诊断。必须提示'本结果仅作为家庭睡眠观察参考，不能替代医学诊断'。",
            {},
            [](const PropertyList&) -> ReturnValue {
                return ReturnValue(SleepDataCenter::GetInstance().GetApneaRiskJson());
            }
        );

        mcp.AddTool(
            "self.sleep.get_night_summary",
            "获取今晚睡眠摘要（时长、呼吸事件、最低血氧、心率/呼吸率均值、风险等级、舒适度），适合语音播报。只基于返回 JSON 回答，不要编造数据。",
            {},
            [](const PropertyList&) -> ReturnValue {
                return ReturnValue(SleepDataCenter::GetInstance().GetNightSummaryJson());
            }
        );

        mcp.AddTool(
            "self.sleep.get_sensor_quality",
            "检查各传感器在线状态（雷达、血氧、环境传感器、波形数据可用性）。只基于返回 JSON 回答，如实反映哪些传感器离线或数据不足。",
            {},
            [](const PropertyList&) -> ReturnValue {
                return ReturnValue(SleepDataCenter::GetInstance().GetSensorQualityJson());
            }
        );

        mcp.AddTool(
            "self.sleep.explain_current_risk",
            "用中文解释当前呼吸风险的来源（哪些证据支持、哪些数据缺失、是否需要关注）。只基于返回 JSON 回答，不要编造数据，不要给出医学诊断。",
            {},
            [](const PropertyList&) -> ReturnValue {
                return ReturnValue(SleepDataCenter::GetInstance().GetNightSummaryJson());
            }
        );

        mcp.AddTool(
            "self.sleep.generate_report",
            "生成完整睡眠监测结构化报告。请根据返回 JSON 生成中文自然语言报告，不要编造未提供的数据，不要给出医学诊断，必须提示'本报告仅作为家庭睡眠观察参考，不能替代医学诊断'。",
            {},
            [](const PropertyList&) -> ReturnValue {
                return ReturnValue(SleepDataCenter::GetInstance().GenerateReportJson());
            }
        );
    }

    void InitializeSt7735Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        ESP_LOGD(TAG, "Install panel IO");
        // 液晶屏控制IO初始化
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = LCD_CS_PIN;
        io_config.dc_gpio_num = LCD_DC_PIN;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 20 * 1000 * 1000;
        io_config.trans_queue_depth = 7;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io);

        // 初始化液晶屏驱动芯片ST7735S (使用st7789驱动兼容模式)
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = LCD_RST_PIN;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR; // 对应MADCTL 0xC8的BGR位
        panel_config.bits_per_pixel = 16;
        panel_config.data_endian = LCD_RGB_DATA_ENDIAN_BIG;
        esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel);
        
        // 背光 GPIO 初始化
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            gpio_config_t bk_gpio_config = {
                .pin_bit_mask = 1ULL << DISPLAY_BACKLIGHT_PIN,
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            gpio_config(&bk_gpio_config);
            gpio_set_level(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT ? 0 : 1);
        }

        esp_lcd_panel_reset(panel);
        xl9555_->SetOutputState(8, 1);
        xl9555_->SetOutputState(2, 0);
        esp_lcd_panel_init(panel);

        // ST7735 REDTAB Gamma校正序列，必须手动发送以确保颜色正常
        uint8_t data0[] = {0x02, 0x1c, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2d, 0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10};
        uint8_t data1[] = {0x03, 0x1d, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D, 0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10};
        esp_lcd_panel_io_tx_param(panel_io, 0xe0, data0, 16);
        esp_lcd_panel_io_tx_param(panel_io, 0xe1, data1, 16);

        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY); 
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    // 初始化摄像头：ov2640；
    // 根据正点原子官方示例参数
    void InitializeCamera() {
        xl9555_->SetOutputState(OV_PWDN_IO, 0); // PWDN=低 (上电)
        xl9555_->SetOutputState(OV_RESET_IO, 0); // 确保复位
        vTaskDelay(pdMS_TO_TICKS(50));           // 延长复位保持时间
        xl9555_->SetOutputState(OV_RESET_IO, 1); // 释放复位
        vTaskDelay(pdMS_TO_TICKS(50));           // 延长 50ms

        static esp_cam_ctlr_dvp_pin_config_t dvp_pin_config = {
            .data_width = CAM_CTLR_DATA_WIDTH_8,
            .data_io = {
                [0] = CAM_PIN_D0,
                [1] = CAM_PIN_D1,
                [2] = CAM_PIN_D2,
                [3] = CAM_PIN_D3,
                [4] = CAM_PIN_D4,
                [5] = CAM_PIN_D5,
                [6] = CAM_PIN_D6,
                [7] = CAM_PIN_D7,
            },
            .vsync_io = CAM_PIN_VSYNC,
            .de_io = CAM_PIN_HREF,
            .pclk_io = CAM_PIN_PCLK,
            .xclk_io = CAM_PIN_XCLK,
        };

        esp_video_init_sccb_config_t sccb_config = {
            .init_sccb = true,
            .i2c_config = {
                .port = 1,
                .scl_pin = CAM_PIN_SIOC,
                .sda_pin = CAM_PIN_SIOD,
            },
            .freq = 100000,
        };

        esp_video_init_dvp_config_t dvp_config = {
            .sccb_config = sccb_config,
            .reset_pin = CAM_PIN_RESET,   // 实际由 XL9555 控制
            .pwdn_pin = CAM_PIN_PWDN,     // 实际由 XL9555 控制
            .dvp_pin = dvp_pin_config,
            .xclk_freq = 20000000,
        };

        esp_video_init_config_t video_config = {
            .dvp = &dvp_config,
        };

        camera_ = new EspVideo(video_config);
    }

public:
    atk_dnesp32s3() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeSt7735Display();
        InitializeButtons();
        InitializeTools();
        // InitializeCamera();  // 摄像头引脚与 SPI 屏幕冲突，已禁用
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8388AudioCodec audio_codec(
            i2c_bus_, 
            I2C_NUM_0, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, 
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, 
            AUDIO_I2S_GPIO_DIN,
            GPIO_NUM_NC, 
            AUDIO_CODEC_ES8388_ADDR
        );
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
    
    virtual Camera* GetCamera() override {
        return camera_;
    }
};

/* C 接口: 暴露 I2C0 bus handle 给 AP3216C 驱动 */
extern "C" {
#include <driver/i2c_master.h>
i2c_master_bus_handle_t board_get_i2c0_handle(void) {
    auto& board = static_cast<atk_dnesp32s3&>(Board::GetInstance());
    return board.GetI2cBus();
}
}

DECLARE_BOARD(atk_dnesp32s3);
