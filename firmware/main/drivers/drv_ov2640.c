/**
 * @file drv_ov2640.c
 * @brief OV2640 2MP camera sensor driver via esp_camera component.
 *        Returns JPEG image capture status via CAP_COUNT (byte count).
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DRV_OV2640";

/* esp_camera component types — declared here to avoid hard dep if not in build */
typedef struct {
    uint8_t *buf;
    size_t   len;
    size_t   width;
    size_t   height;
    uint8_t  format;       /* 4 = JPEG */
    int64_t  timestamp_us;
} camera_fb_t;

/* Weak externs — resolved by esp_camera component if present */
__attribute__((weak)) esp_err_t esp_camera_init(const void *config) { return ESP_ERR_NOT_SUPPORTED; }
__attribute__((weak)) camera_fb_t *esp_camera_fb_get(void) { return NULL; }
__attribute__((weak)) void esp_camera_fb_return(camera_fb_t *fb) { (void)fb; }

typedef struct {
    bool    initialized;
    size_t  last_size;
    size_t  last_width;
    size_t  last_height;
} ov2640_state_t;

static ov2640_state_t s_state[1];
static uint8_t s_count = 0;

/* Minimal camera config type — matches esp-idf camera_config_t layout */
typedef struct {
    int pin_pwdn, pin_reset, pin_xclk;
    int pin_sccb_sda, pin_sccb_scl;
    int pin_d7, pin_d6, pin_d5, pin_d4, pin_d3, pin_d2, pin_d1, pin_d0;
    int pin_vsync, pin_href, pin_pclk;
    int xclk_freq_hz;
    int ledc_timer, ledc_channel;
    int pixel_format;   /* 4=JPEG */
    int frame_size;     /* 5=QVGA, 6=VGA, 13=SVGA */
    int jpeg_quality;
    int fb_count;
    int grab_mode;
} camera_config_t;

static esp_err_t ov2640_init(const driver_config_t *cfg) {
    if (s_count >= 1) return ESP_ERR_NO_MEM;
    ov2640_state_t *st = &s_state[0];

    camera_config_t cam = {
        .pin_pwdn       = cfg->pins[0].gpio_num,
        .pin_reset      = -1,
        .pin_xclk       = cfg->pins[1].gpio_num,
        .pin_sccb_sda   = cfg->pins[2].gpio_num,
        .pin_sccb_scl   = cfg->pins[3].gpio_num,
        /* Y2-Y9 data pins default for WROVER-E */
        .pin_d7=35,.pin_d6=34,.pin_d5=39,.pin_d4=36,
        .pin_d3=21,.pin_d2=19,.pin_d1=18,.pin_d0=5,
        .pin_vsync=25,.pin_href=23,.pin_pclk=22,
        .xclk_freq_hz = 20000000,
        .ledc_timer   = 0,
        .ledc_channel = 0,
        .pixel_format = 4,  /* JPEG */
        .frame_size   = 6,  /* VGA 640x480 */
        .jpeg_quality = 12,
        .fb_count     = 1,
        .grab_mode    = 0,
    };

    esp_err_t err = esp_camera_init(&cam);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "esp_camera not linked — OV2640 in stub mode");
        } else {
            ESP_LOGE(TAG, "esp_camera_init failed: %d", err);
            return err;
        }
    }

    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "OV2640 init OK");
    return ESP_OK;
}

static esp_err_t ov2640_read(driver_handle_t h, capability_t field, sensor_value_t *out) {
    if (h.driver_index >= s_count) return ESP_ERR_INVALID_ARG;
    ov2640_state_t *st = &s_state[0];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    if (field != CAP_COUNT) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
        st->last_size   = fb->len;
        st->last_width  = fb->width;
        st->last_height = fb->height;
        esp_camera_fb_return(fb);
    }

    out->type = VAL_TYPE_INT;
    out->i = (int32_t)st->last_size;
    out->capability = CAP_COUNT;
    out->error = (st->last_size > 0) ? DRV_OK : DRV_ERR_TIMEOUT;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return ESP_OK;
}

static esp_err_t ov2640_deinit(driver_handle_t h) {
    (void)h;
    if (s_count > 0) s_state[0].initialized = false;
    return ESP_OK;
}

static const driver_meta_t ov2640_meta = {
    .name = "drv_ov2640", .display_name = "OV2640 2MP Camera",
    .version = "1.0.0", .type = DRIVER_TYPE_SENSOR, .bus_type = BUS_TYPE_SPI,
    .capabilities = {CAP_COUNT}, .num_capabilities = 1,
    .max_latency_us = 200000, .min_interval_ms = 100,
    .failure_modes = {{DRV_ERR_TIMEOUT, "No frame captured", {.type=VAL_TYPE_INT,.i=0}}},
    .num_failure_modes = 1, .internal_state_size = sizeof(ov2640_state_t),
};

const driver_vtable_t drv_ov2640_vtable = {
    .init=ov2640_init, .read=ov2640_read, .write=NULL, .deinit=ov2640_deinit, .meta=&ov2640_meta
};
