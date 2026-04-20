/**
 * @file pal_onewire.c
 * @brief Platform-agnostic 1-Wire bit-bang implementation.
 *
 * Uses only pal_gpio_set_direction / pal_gpio_set_level / pal_gpio_get_level
 * and pal_delay_us — all provided by the platform PAL.
 * Timing follows Maxim/Dallas AN 126 (standard speed).
 */

#include "parakram_pal.h"

/* ── Reset pulse ─────────────────────────────────────────────────────────── */

pal_err_t pal_onewire_reset(int pin)
{
    /* Drive low 480 µs → release → wait 70 µs → sample → wait 410 µs */
    pal_gpio_set_direction(pin, PAL_GPIO_OUTPUT, false, false);
    pal_gpio_set_level(pin, 0);
    pal_delay_us(480);

    pal_gpio_set_direction(pin, PAL_GPIO_INPUT, false, false);
    pal_delay_us(70);

    int present = !pal_gpio_get_level(pin);   /* LOW = device present */
    pal_delay_us(410);

    return present ? PAL_OK : PAL_ERR_NOT_FOUND;
}

/* ── Bit write ───────────────────────────────────────────────────────────── */

void pal_onewire_write_bit(int pin, uint8_t bit)
{
    if (bit) {
        /* Write-1: pull low 6 µs, release 64 µs */
        pal_gpio_set_direction(pin, PAL_GPIO_OUTPUT, false, false);
        pal_gpio_set_level(pin, 0);
        pal_delay_us(6);
        pal_gpio_set_level(pin, 1);
        pal_delay_us(64);
    } else {
        /* Write-0: pull low 60 µs, release 10 µs */
        pal_gpio_set_direction(pin, PAL_GPIO_OUTPUT, false, false);
        pal_gpio_set_level(pin, 0);
        pal_delay_us(60);
        pal_gpio_set_level(pin, 1);
        pal_delay_us(10);
    }
}

/* ── Bit read ────────────────────────────────────────────────────────────── */

uint8_t pal_onewire_read_bit(int pin)
{
    /* Initiate read slot: pull low 6 µs, release, sample at 9 µs, wait 55 µs */
    pal_gpio_set_direction(pin, PAL_GPIO_OUTPUT, false, false);
    pal_gpio_set_level(pin, 0);
    pal_delay_us(6);

    pal_gpio_set_direction(pin, PAL_GPIO_INPUT, false, false);
    pal_delay_us(9);

    uint8_t bit = (uint8_t)pal_gpio_get_level(pin);
    pal_delay_us(55);
    return bit;
}

/* ── Byte write (LSB first) ──────────────────────────────────────────────── */

void pal_onewire_write_byte(int pin, uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        pal_onewire_write_bit(pin, byte & 0x01);
        byte >>= 1;
    }
}

/* ── Byte read (LSB first) ───────────────────────────────────────────────── */

uint8_t pal_onewire_read_byte(int pin)
{
    uint8_t result = 0;
    for (int i = 0; i < 8; i++) {
        result |= (uint8_t)(pal_onewire_read_bit(pin) << i);
    }
    return result;
}
