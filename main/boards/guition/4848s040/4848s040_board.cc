#include "wifi_board.h"
#include "board.h"
#include "display/lcd_display.h"
#include "config.h"
#include "audio_codec.h"
#include "codecs/dummy_audio_codec.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include <esp_lcd_panel_io_additions.h>
#include "esp_lvgl_port.h"
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <memory>

static const char *TAG = "Guition4848S040Board";

// 触摸数据队列，用于触摸任务和LVGL回调之间传递坐标
static QueueHandle_t touch_data_queue = NULL;

// --- LVGL读取回调（非阻塞）---
static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    // 仅从队列取数据，不执行任何I2C操作
    if (xQueueReceive(touch_data_queue, data, 0) != pdTRUE) {
        // 队列为空，保持上一次状态或视为释放
        data->state = LV_INDEV_STATE_RELEASED;
    }
}


// =========================================================================
// 1. ST7701S 初始化命令序列 (参考 abcbbck-png 项目及 ST7701S 数据手册)
//    不同批次的屏幕初始化序列可能微调，若显示异常请优先检查此处
// =========================================================================
static const st7701_lcd_init_cmd_t st7701_init_cmds[] = {
    // Command page selection
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, (uint8_t []){0x08}, 1, 0},
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},

    // Display settings (values from Arduino_GFX reference project)
    {0xC0, (uint8_t []){0x3B, 0x00}, 2, 0},       // LNESET
    {0xC1, (uint8_t []){0x0D, 0x02}, 2, 0},       // PORCTRL: VBP=13, VFP=2
    {0xC2, (uint8_t []){0x31, 0x05}, 2, 0},       // INVSET (from reference, was 0x21,0x08)
    {0xCD, (uint8_t []){0x00}, 1, 0},             // 18-bit/pixel mode

    // Gamma settings (positive)
    {0xB0, (uint8_t []){0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08, 0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18}, 16, 0},
    // Gamma settings (negative)
    {0xB1, (uint8_t []){0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08, 0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18}, 16, 0},

    // Power control
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t []){0x60}, 1, 0},
    {0xB1, (uint8_t []){0x30}, 1, 0},
    {0xB2, (uint8_t []){0x87}, 1, 0},
    {0xB3, (uint8_t []){0x80}, 1, 0},
    {0xB5, (uint8_t []){0x49}, 1, 0},
    {0xB7, (uint8_t []){0x85}, 1, 0},
    {0xB8, (uint8_t []){0x21}, 1, 0},
    {0xC1, (uint8_t []){0x78}, 1, 0},
    {0xC2, (uint8_t []){0x78}, 1, 20},  // 20ms delay

    // GIP settings
    {0xE0, (uint8_t []){0x00, 0x1B, 0x02}, 3, 0},
    {0xE1, (uint8_t []){0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44}, 11, 0},
    {0xE2, (uint8_t []){0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00}, 12, 0},
    {0xE3, (uint8_t []){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE4, (uint8_t []){0x44, 0x44}, 2, 0},
    {0xE5, (uint8_t []){0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0, 0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0}, 16, 0},
    {0xE6, (uint8_t []){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE7, (uint8_t []){0x44, 0x44}, 2, 0},
    {0xE8, (uint8_t []){0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0, 0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0}, 16, 0},
    {0xEB, (uint8_t []){0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40}, 7, 0},
    {0xEC, (uint8_t []){0x3C, 0x00}, 2, 0},
    {0xED, (uint8_t []){0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA}, 16, 0},

    // Return to normal page
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},

    // Pixel format / orientation (matches manufacturer TXT)
    {0x3A, (uint8_t []){0x66}, 1, 0},           // RGB565 format
    {0x36, (uint8_t []){0x00}, 1, 0},           // MADCTL default

    // Sleep out
    {0x11, (uint8_t []){0x00}, 0, 120},  // 120ms delay

    // Display on
    {0x29, (uint8_t []){0x00}, 0, 20},   // 20ms delay
};

// =========================================================================
// 板级类
// =========================================================================
class Guition4848S040Board : public WifiBoard {
public:
    Guition4848S040Board() {
        ESP_LOGI(TAG, "Initializing Guition ESP32-4848S040");

        // 1. I2C (触摸使用)
        InitializeI2c();

        // 2. RGB 面板 (ST7701S)
        if (!InitializeSt7701sRgbPanel()) {
            ESP_LOGE(TAG, "ST7701S init failed");
            return;
        }

        // 3. 触摸 (GT911)—失败不阻断启动
        InitializeGt911Touch();

        // 4. 创建显示对象 (内部会初始化 LVGL 和 lvgl_port)
        CreateDisplay();

        // 5. 将触摸注册到 LVGL
        RegisterTouch();

        // 6. 背光
        InitializeBacklight();

        ESP_LOGI(TAG, "Hardware initialization done");
    }

    ~Guition4848S040Board() override = default;

    // ---------------------------------------------------------------------
    // 音频编解码器
    // 硬件上 I2S (GPIO 40/2/1) 与继电器复用，需移动 R25/R26/R27 到
    // R21/R22/R23 才能使用。未改硬件前返回哑元，避免上层崩溃。
    // ---------------------------------------------------------------------
    AudioCodec *GetAudioCodec() override {
        return new DummyAudioCodec(16000, 24000);
    }

    Display *GetDisplay() override {
        return display_.get();
    }

private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    esp_lcd_touch_handle_t touch_ = nullptr;
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    std::unique_ptr<RgbLcdDisplay> display_;

    // ---------------------------------------------------------------------
    // I2C 初始化 (GT911)
    // ---------------------------------------------------------------------
    void InitializeI2c() {
        i2c_master_bus_config_t i2c_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = TP_I2C_SDA_PIN,
            .scl_io_num = TP_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &i2c_bus_));
        ESP_LOGI(TAG, "I2C0 ready (SDA=%d, SCL=%d)", TP_I2C_SDA_PIN, TP_I2C_SCL_PIN);
    }

    // ---------------------------------------------------------------------
    // ST7701S RGB 面板初始化
    // ---------------------------------------------------------------------
    bool InitializeSt7701sRgbPanel() {
        ESP_LOGI(TAG, "Installing 3-wire SPI panel IO (ST7701S)");

        spi_line_config_t line_config = {};
        line_config.cs_io_type = IO_TYPE_GPIO;
        line_config.cs_gpio_num = LCD_SPI_CS_PIN_NUM;
        line_config.scl_io_type = IO_TYPE_GPIO;
        line_config.scl_gpio_num = LCD_SPI_SCK_PIN_NUM;
        line_config.sda_io_type = IO_TYPE_GPIO;
        line_config.sda_gpio_num = LCD_SPI_SDA_PIN_NUM;
        line_config.io_expander = nullptr;

        esp_lcd_panel_io_3wire_spi_config_t io_config =
            ST7701_PANEL_IO_3WIRE_SPI_CONFIG(line_config, 0);

        if (esp_lcd_new_panel_io_3wire_spi(&io_config, &panel_io_) != ESP_OK) {
            ESP_LOGE(TAG, "3-wire SPI IO creation failed");
            return false;
        }

        // -------- RGB 面板配置 --------
        esp_lcd_rgb_panel_config_t rgb_config = {};
        rgb_config.clk_src = LCD_CLK_SRC_DEFAULT;
        rgb_config.data_width = 16;
        rgb_config.in_color_format = LCD_COLOR_FMT_RGB565;
        rgb_config.out_color_format = LCD_COLOR_FMT_RGB565;
        rgb_config.num_fbs = 2;
        rgb_config.bounce_buffer_size_px = 20 * DISPLAY_WIDTH;
        rgb_config.dma_burst_size = 64;
        rgb_config.disp_gpio_num = GPIO_NUM_NC;
        rgb_config.pclk_gpio_num = LCD_RGB_PCLK_PIN_NUM;
        rgb_config.vsync_gpio_num = LCD_RGB_VSYNC_PIN_NUM;
        rgb_config.hsync_gpio_num = LCD_RGB_HSYNC_PIN_NUM;
        rgb_config.de_gpio_num = LCD_RGB_DE_PIN_NUM;
        rgb_config.data_gpio_nums[0]  = LCD_RGB_DATA_PIN_NUM_B0;
        rgb_config.data_gpio_nums[1]  = LCD_RGB_DATA_PIN_NUM_B1;
        rgb_config.data_gpio_nums[2]  = LCD_RGB_DATA_PIN_NUM_B2;
        rgb_config.data_gpio_nums[3]  = LCD_RGB_DATA_PIN_NUM_B3;
        rgb_config.data_gpio_nums[4]  = LCD_RGB_DATA_PIN_NUM_B4;
        rgb_config.data_gpio_nums[5]  = LCD_RGB_DATA_PIN_NUM_G0;
        rgb_config.data_gpio_nums[6]  = LCD_RGB_DATA_PIN_NUM_G1;
        rgb_config.data_gpio_nums[7]  = LCD_RGB_DATA_PIN_NUM_G2;
        rgb_config.data_gpio_nums[8]  = LCD_RGB_DATA_PIN_NUM_G3;
        rgb_config.data_gpio_nums[9]  = LCD_RGB_DATA_PIN_NUM_G4;
        rgb_config.data_gpio_nums[10] = LCD_RGB_DATA_PIN_NUM_G5;
        rgb_config.data_gpio_nums[11] = LCD_RGB_DATA_PIN_NUM_R0;
        rgb_config.data_gpio_nums[12] = LCD_RGB_DATA_PIN_NUM_R1;
        rgb_config.data_gpio_nums[13] = LCD_RGB_DATA_PIN_NUM_R2;
        rgb_config.data_gpio_nums[14] = LCD_RGB_DATA_PIN_NUM_R3;
        rgb_config.data_gpio_nums[15] = LCD_RGB_DATA_PIN_NUM_R4;

        // 时序参数
        rgb_config.timings.pclk_hz = 12 * 1000 * 1000;
        rgb_config.timings.h_res = DISPLAY_WIDTH;
        rgb_config.timings.v_res = DISPLAY_HEIGHT;
        rgb_config.timings.hsync_pulse_width = 10;
        rgb_config.timings.hsync_back_porch  = 50;
        rgb_config.timings.hsync_front_porch = 20;
        rgb_config.timings.vsync_pulse_width = 8;
        rgb_config.timings.vsync_back_porch  = 10;
        rgb_config.timings.vsync_front_porch = 10;
        rgb_config.timings.flags.pclk_active_neg = 1;
        rgb_config.flags.fb_in_psram = 1;

        // -------- ST7701 厂商配置 --------
        st7701_vendor_config_t vendor_config = {};
        vendor_config.rgb_config = &rgb_config;
        vendor_config.init_cmds = st7701_init_cmds;
        vendor_config.init_cmds_size = sizeof(st7701_init_cmds) / sizeof(st7701_init_cmds[0]);
        vendor_config.flags.mirror_by_cmd = 1;
        vendor_config.flags.enable_io_multiplex = 0;

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        panel_config.vendor_config = &vendor_config;

        if (esp_lcd_new_panel_st7701(panel_io_, &panel_config, &panel_) != ESP_OK) {
            ESP_LOGE(TAG, "esp_lcd_new_panel_st7701 failed");
            return false;
        }

        esp_lcd_panel_reset(panel_);
        esp_lcd_panel_init(panel_);
        ESP_LOGI(TAG, "ST7701S initialized");
        return true;
    }

    // ---------------------------------------------------------------------
    // GT911 触摸初始化 (仅创建句柄，稍后注册到 LVGL)
    // ---------------------------------------------------------------------
    void InitializeGt911Touch() {
        esp_lcd_touch_config_t tp_cfg = {};
        tp_cfg.x_max = DISPLAY_WIDTH - 1;
        tp_cfg.y_max = DISPLAY_HEIGHT - 1;
        tp_cfg.rst_gpio_num = GPIO_NUM_NC;
        tp_cfg.int_gpio_num = GPIO_NUM_NC;
        tp_cfg.levels.reset = 0;
        tp_cfg.levels.interrupt = 0;
        tp_cfg.flags.swap_xy = DISPLAY_SWAP_XY;
        tp_cfg.flags.mirror_x = DISPLAY_MIRROR_X;
        tp_cfg.flags.mirror_y = DISPLAY_MIRROR_Y;

        esp_lcd_panel_io_handle_t tp_io = nullptr;
        esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
        tp_io_config.transaction_timeout_ms = 100; // 100ms 后强制返回超时

        if (esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io) != ESP_OK) {
            ESP_LOGW(TAG, "GT911 IO create failed");
            return;
        }

        if (esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &touch_) != ESP_OK) {
            ESP_LOGW(TAG, "GT911 driver init failed");
            touch_ = nullptr;
            return;
        }
        ESP_LOGI(TAG, "GT911 ready");
    }

    // ---------------------------------------------------------------------
    // 创建 RgbLcdDisplay (内部完成 LVGL 和 lvgl_port 初始化)
    // ---------------------------------------------------------------------
    void CreateDisplay() {
        display_ = std::make_unique<RgbLcdDisplay>(
            panel_io_,
            panel_,
            DISPLAY_WIDTH,
            DISPLAY_HEIGHT,
            0,                      // offset_x
            0,                      // offset_y
            DISPLAY_MIRROR_X,
            DISPLAY_MIRROR_Y,
            DISPLAY_SWAP_XY
        );

        ESP_LOGI(TAG, "RgbLcdDisplay created");
    }


private:
    // ... 原有的 panel_io_、panel_、touch_、i2c_bus_、display_ 定义保持不变 ...

    // ---------------------------------------------------------------------
    // 软件 I2C 总线清除：手动翻转 SCL 最多 9 个时钟，让从机释放 SDA
    // ---------------------------------------------------------------------
    esp_err_t I2cSoftwareBusClear(gpio_num_t scl, gpio_num_t sda) {
        ESP_LOGW("I2C_RECOVERY", "Attempting software bus clear on SCL=%d, SDA=%d", scl, sda);

        gpio_set_direction(scl, GPIO_MODE_INPUT_OUTPUT_OD);
        gpio_set_direction(sda, GPIO_MODE_INPUT_OUTPUT_OD);
        gpio_set_level(scl, 1);
        gpio_set_level(sda, 1);
        esp_rom_delay_us(10);

        if (gpio_get_level(sda) == 0) {
            for (int i = 0; i < 9; i++) {
                gpio_set_level(scl, 0);
                esp_rom_delay_us(10);
                gpio_set_level(scl, 1);
                esp_rom_delay_us(10);
                if (gpio_get_level(sda) == 1) {
                    ESP_LOGI("I2C_RECOVERY", "SDA released after %d clock(s)", i + 1);
                    break;
                }
            }
        }

        // 补一个 STOP 条件
        gpio_set_level(scl, 0);
        gpio_set_level(sda, 0);
        esp_rom_delay_us(10);
        gpio_set_level(scl, 1);
        esp_rom_delay_us(10);
        gpio_set_level(sda, 1);
        esp_rom_delay_us(10);

        if (gpio_get_level(sda) == 0 || gpio_get_level(scl) == 0) {
            ESP_LOGE("I2C_RECOVERY", "Bus still held low after software clear (SDA=%d, SCL=%d)",
                     gpio_get_level(sda), gpio_get_level(scl));
            return ESP_FAIL;
        }
        ESP_LOGI("I2C_RECOVERY", "Software bus clear succeeded");
        return ESP_OK;
    }

    // ---------------------------------------------------------------------
    // 分层恢复：硬件 FSM 复位 → 软件 SCL 清除 → 再次硬件复位
    // ---------------------------------------------------------------------
    void I2cBusRecovery() {
        if (i2c_bus_ == nullptr) {
            ESP_LOGE("I2C_RECOVERY", "i2c_bus_ is null, cannot recover");
            return;
        }

        esp_err_t ret = i2c_master_bus_reset(i2c_bus_);
        if (ret == ESP_OK) {
            ESP_LOGW("I2C_RECOVERY", "Hardware bus reset succeeded");
            return;
        }

        ESP_LOGW("I2C_RECOVERY", "Hardware reset failed (%s), falling back to software clear",
                 esp_err_to_name(ret));

        ret = I2cSoftwareBusClear((gpio_num_t)TP_I2C_SCL_PIN, (gpio_num_t)TP_I2C_SDA_PIN);
        if (ret != ESP_OK) {
            ESP_LOGE("I2C_RECOVERY", "Software clear also failed, bus is hard-stuck");
            return;
        }

        ret = i2c_master_bus_reset(i2c_bus_);
        if (ret == ESP_OK) {
            ESP_LOGI("I2C_RECOVERY", "Post-clear hardware reset succeeded");
        } else {
            ESP_LOGE("I2C_RECOVERY", "Post-clear hardware reset failed: %s",
                     esp_err_to_name(ret));
        }
    }

    // ---------------------------------------------------------------------
    // 独立的触摸轮询任务（static，通过 arg 传入 this）
    // ---------------------------------------------------------------------
    static void TouchPollingTask(void *arg) {
        Guition4848S040Board *self = static_cast<Guition4848S040Board *>(arg);
        esp_lcd_touch_handle_t tp = self->touch_;
        const TickType_t max_block_time = pdMS_TO_TICKS(100);
        int consecutive_failures = 0;

        while (1) {
            TickType_t start_time = xTaskGetTickCount();
            esp_err_t ret = esp_lcd_touch_read_data(tp);
            TickType_t elapsed = xTaskGetTickCount() - start_time;

            if (ret == ESP_OK && elapsed < max_block_time) {
                consecutive_failures = 0;
                uint8_t point_num = 0;
                esp_lcd_touch_point_data_t point_data[1];
                esp_err_t get_ret = esp_lcd_touch_get_data(tp, point_data, &point_num, 1);

                lv_indev_data_t data = {};
                if (get_ret == ESP_OK && point_num > 0) {
                    data.point.x = point_data[0].x;
                    data.point.y = point_data[0].y;
                    data.state = LV_INDEV_STATE_PRESSED;
                } else {
                    data.state = LV_INDEV_STATE_RELEASED;
                }
                xQueueSend(touch_data_queue, &data, 0);
            } else {
                lv_indev_data_t data = {};
                data.state = LV_INDEV_STATE_RELEASED;
                xQueueSend(touch_data_queue, &data, 0);

                consecutive_failures++;
                ESP_LOGW("TouchTask", "I2C read failed (%s), consecutive=%d",
                         esp_err_to_name(ret), consecutive_failures);

                if (consecutive_failures >= 3) {
                    self->I2cBusRecovery();   // 通过 self 访问私有成员
                    consecutive_failures = 0;
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    // ---------------------------------------------------------------------
    // 将触摸句柄注册到 LVGL
    // ---------------------------------------------------------------------
    void RegisterTouch() {
        if (touch_ == nullptr) {
            ESP_LOGW(TAG, "No touch handle, skip registration");
            return;
        }
        if (display_ == nullptr) {
            ESP_LOGE(TAG, "Display is null, cannot register touch");
            return;
        }

        // 1. 创建触摸数据队列
        touch_data_queue = xQueueCreate(10, sizeof(lv_indev_data_t));
        if (touch_data_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create touch data queue");
            return;
        }

        // 2. 创建LVGL输入设备并设置自定义读取回调
        lv_display_t* lv_disp = lv_display_get_default();
        if (lv_disp == nullptr) {
            ESP_LOGE(TAG, "LVGL display is null, cannot register touch");
            vQueueDelete(touch_data_queue);
            return;
        }

        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(indev, lv_disp);
        // 关键：使用你自己的非阻塞回调，替代 lvgl_port_add_touch
        lv_indev_set_read_cb(indev, lvgl_touch_read_cb);

        // 3. 启动独立的低优先级触摸轮询任务
        // 优先级应低于LVGL任务（通常LVGL任务优先级为0或1，触摸任务可设为1或2，但需确保不抢占UI）
        xTaskCreate(TouchPollingTask, "touch_poll", 4096, this, 2, NULL);

        ESP_LOGI(TAG, "Touch registered to LVGL (async mode)");
    }

    // ---------------------------------------------------------------------
    // 背光 GPIO38
    // ---------------------------------------------------------------------
    void InitializeBacklight() {
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = (uint64_t)1ULL << DISPLAY_BACKLIGHT_PIN;
        cfg.mode = GPIO_MODE_OUTPUT;
        cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&cfg);
        gpio_set_level(DISPLAY_BACKLIGHT_PIN, 1);
        ESP_LOGI(TAG, "Backlight GPIO%d enabled", DISPLAY_BACKLIGHT_PIN);
    }
};

// =========================================================================
// 注册开发板
// =========================================================================
DECLARE_BOARD(Guition4848S040Board);
