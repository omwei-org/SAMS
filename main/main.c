#include <stdint.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "slc_gate.h"

#define LCD_WIDTH 172
#define LCD_HEIGHT 320
#define LCD_PIN_MOSI 45
#define LCD_PIN_SCLK 40
#define LCD_PIN_CS 42
#define LCD_PIN_DC 41
#define LCD_PIN_RST 39
#define LCD_PIN_BL 48

static const char *TAG = "slc_gate";
static esp_lcd_panel_handle_t panel;

static esp_err_t lcd_init(void)
{
    ESP_LOGI(TAG, "LCD init: Starting");
    gpio_config_t backlight = {
        .pin_bit_mask = 1ULL << LCD_PIN_BL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&backlight), TAG, "backlight GPIO config failed");
    ESP_LOGI(TAG, "LCD init: Backlight GPIO configured");
    ESP_RETURN_ON_ERROR(gpio_set_level(LCD_PIN_BL, 1), TAG, "backlight enable failed");
    ESP_LOGI(TAG, "LCD init: Backlight enabled");

    spi_bus_config_t bus = {
        .sclk_io_num = LCD_PIN_SCLK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * 20 * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "SPI init failed");
    ESP_LOGI(TAG, "LCD init: SPI bus initialized");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io),
        TAG, "LCD SPI IO init failed");
    ESP_LOGI(TAG, "LCD init: SPI IO initialized");

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &panel_config, &panel), TAG, "ST7789 init failed");
    ESP_LOGI(TAG, "LCD init: ST7789 panel created");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "LCD reset failed");
    ESP_LOGI(TAG, "LCD init: Panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "LCD panel init failed");
    ESP_LOGI(TAG, "LCD init: Panel initialized");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel, true), TAG, "LCD invert failed");
    ESP_LOGI(TAG, "LCD init: Color inverted");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "LCD display enable failed");
    ESP_LOGI(TAG, "LCD init: Display enabled");
    return ESP_OK;
}

static esp_err_t lcd_fill(uint16_t color)
{
    uint16_t *lines = malloc(LCD_WIDTH * 20 * sizeof(uint16_t));
    if (lines == NULL) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < LCD_WIDTH * 20; ++i) {
        lines[i] = color;
    }
    esp_err_t result = ESP_OK;
    for (int y = 0; y < LCD_HEIGHT && result == ESP_OK; y += 20) {
        result = esp_lcd_panel_draw_bitmap(panel, 0, y, LCD_WIDTH, y + 20, lines);
    }
    free(lines);
    return result;
}

typedef struct {
    uint32_t virtual_effect_count;
} virtual_effect_t;

static bool virtual_effect(void *context)
{
    virtual_effect_t *effect = (virtual_effect_t *)context;
    if (effect == NULL) {
        return false;
    }
    ++effect->virtual_effect_count;
    ESP_LOGI(TAG, "VIRTUAL_EFFECT_COMMITTED count=%lu", (unsigned long)effect->virtual_effect_count);
    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "SLC Execution Gate prototype boot");
    ESP_LOGI(TAG, "Board: Waveshare ESP32-S3-LCD-1.47 (not 1.47B)");

    if (lcd_init() != ESP_OK) {
        ESP_LOGE(TAG, "LCD init failed; gate demo will continue on serial only");
    } else {
        ESP_LOGI(TAG, "LCD init successful, filling with white");
        if (lcd_fill(0xFFFF) != ESP_OK) {
            ESP_LOGE(TAG, "LCD white fill failed");
        } else {
            ESP_LOGI(TAG, "LCD white fill OK, waiting 2s");
            vTaskDelay(pdMS_TO_TICKS(2000));
            ESP_LOGI(TAG, "Now filling with blue");
            if (lcd_fill(0x001F) != ESP_OK) {
                ESP_LOGE(TAG, "LCD blue fill failed");
            }
        }
    }

    slc_gate_t gate;
    slc_gate_init(&gate, 7);

    virtual_effect_t effect = {0};
    slc_request_t request = {
        .target_id = 1,
        .action_id = 10,
        .payload_digest = 0xA17E1234u,
        .nonce = 0x1001u,
    };
    slc_authorization_t authorization = {
        .target_id = 1,
        .action_id = 10,
        .payload_digest = 0xA17E1234u,
        .epoch = 7,
        .nonce = 0x1001u,
        .not_before = 100,
        .not_after = 200,
        .signature_valid = true,
        .issuer_authorized = true,
    };

    slc_result_t result = slc_gate_execute(&gate, &request, &authorization, 150, virtual_effect, &effect);
    ESP_LOGI(TAG, "Valid request result=%s effect_count=%lu",
             slc_result_name(result), (unsigned long)effect.virtual_effect_count);

    slc_result_t replay = slc_gate_execute(&gate, &request, &authorization, 150, virtual_effect, &effect);
    ESP_LOGI(TAG, "Replay result=%s effect_count=%lu",
             slc_result_name(replay), (unsigned long)effect.virtual_effect_count);

    if (panel != NULL) {
        /* Green band means the expected COMMIT happened; red band means it did not. */
        (void)lcd_fill(result == SLC_RESULT_COMMIT ? 0x07E0 : 0xF800);
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
