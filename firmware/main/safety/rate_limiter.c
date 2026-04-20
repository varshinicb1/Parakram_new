/**
 * @file rate_limiter.c
 * @brief Per-driver rate limiter.
 */

#include "safety.h"
#include "esp_timer.h"

static inline uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

void rate_limiter_init(rate_limiter_t *rl, uint32_t min_interval_ms) {
    rl->min_interval_ms = min_interval_ms;
    rl->last_call_tick = 0;
}

bool rate_limiter_check(rate_limiter_t *rl) {
    uint32_t now = now_ms();
    if ((now - rl->last_call_tick) >= rl->min_interval_ms) {
        rl->last_call_tick = now;
        return true;
    }
    return false;
}
