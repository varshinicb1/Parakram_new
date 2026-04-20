/**
 * @file gpio_hal.c
 * @brief GPIO hardware abstraction — pin claiming and safe pin validation.
 */

#include "esp_log.h"
#include "driver/gpio.h"
#include <stdbool.h>

static const char *TAG = "GPIO_HAL";

/* VDYT-S3-R1 safe pins — only these pins may be used */
static const int SAFE_PINS[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 15, 16, 17, 18,
    21, 35, 38, 39, 40, 41, 42, 45, 46, 47, 48
};
#define NUM_SAFE_PINS (sizeof(SAFE_PINS) / sizeof(SAFE_PINS[0]))

static bool s_claimed[50] = {false};

bool gpio_hal_is_safe_pin(int pin) {
    for (int i = 0; i < NUM_SAFE_PINS; i++) {
        if (SAFE_PINS[i] == pin) return true;
    }
    return false;
}

bool gpio_hal_claim(int pin) {
    if (!gpio_hal_is_safe_pin(pin)) {
        ESP_LOGE(TAG, "Pin %d is NOT in safe pin list", pin);
        return false;
    }
    if (pin < 50 && s_claimed[pin]) {
        ESP_LOGE(TAG, "Pin %d already claimed", pin);
        return false;
    }
    s_claimed[pin] = true;
    return true;
}

void gpio_hal_release(int pin) {
    if (pin < 50) s_claimed[pin] = false;
}
