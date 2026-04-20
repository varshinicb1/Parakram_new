#include "lvgl.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <inttypes.h>

static const char *TAG = "LVGL_HAL";

// The width and height map to ST7789 or ILI9341 sizes
#define DISP_BUF_SIZE (240 * 10)

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1;

// This would normally be injected by the Parakram Bytecode Driver initialization
extern void lcd_spi_send_cmd(uint8_t cmd);
extern void lcd_spi_send_data(const uint8_t *data, size_t len);

static void disp_flush_cb(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    // Parakram SPI Display flush logic
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    uint32_t size = w * h * sizeof(lv_color_t);

    // Command to set column and row address (Standard TFT sequence 0x2A / 0x2B / 0x2C)
    // Send 0x2A, x1, x2
    // Send 0x2B, y1, y2
    // Send 0x2C, (data)
    
    // As a generic HAL, we assume the specific `drv_st7789` or `drv_ili9341` exports a `flush` bounds function
    // For this boilerplate, we'll simulate the generic byte transfer logic.
    lcd_spi_send_data((const uint8_t *)color_p, size);

    lv_disp_flush_ready(disp_drv);
}

void lvgl_hal_init(uint32_t screen_w, uint32_t screen_h)
{
    ESP_LOGI(TAG, "Initializing LVGL Base HAL (%" PRIu32 "x%" PRIu32 ")", screen_w, screen_h);
    lv_init();

    // Allocate draw buffer in DMA capable memory if possible, otherwise DRAM
    buf1 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if(!buf1) {
        ESP_LOGW(TAG, "DMA Malloc failed for LVGL buffer, falling back to standard RAM");
        buf1 = malloc(DISP_BUF_SIZE * sizeof(lv_color_t));
    }
    
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, DISP_BUF_SIZE);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    
    disp_drv.hor_res = screen_w;
    disp_drv.ver_res = screen_h;
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    
    lv_disp_drv_register(&disp_drv);
    ESP_LOGI(TAG, "LVGL HAL Registered successfully. Memory allocated.");
}

void lvgl_hal_tick(void)
{
    // Normally called every MS via esp_timer, handled by Parakram scheduler
    lv_tick_inc(1);
    lv_task_handler();
}
