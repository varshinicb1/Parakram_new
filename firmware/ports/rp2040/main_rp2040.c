/**
 * @file main_rp2040.c
 * @brief Parakram firmware entry point for Raspberry Pi RP2040.
 *
 * Initialises the PAL, starts the VM and scheduler, then parks the
 * main core in the idle loop. Core 1 runs the comms stack (USB CDC).
 */

#include "pico/stdlib.h"
#include "parakram_pal.h"
#include "driver_registry.h"
#include "vm.h"
#include "scheduler.h"
#include "event_bus.h"
#include "state_store.h"
#include "watchdog.h"

/* Board-specific pin assignments (Parakram RP2040 reference board) */
#define PIN_I2C0_SDA   4
#define PIN_I2C0_SCL   5
#define PIN_I2C1_SDA   6
#define PIN_I2C1_SCL   7
#define PIN_SPI0_SCK   18
#define PIN_SPI0_MOSI  19
#define PIN_SPI0_MISO  16
#define PIN_UART0_TX   0
#define PIN_UART0_RX   1
#define PIN_STATUS_LED 25  /* onboard LED */

static void core1_comms_task(void)
{
    /* USB CDC serial bridge — receives bytecode frames over USB, dispatches
     * to event_bus for the VM to pick up. Runs on core 1. */
    while (1) {
        int c = getchar_timeout_us(1000);
        if (c != PICO_ERROR_TIMEOUT) {
            event_bus_push_byte((uint8_t)c);
        }
        pal_feed_watchdog();
    }
}

int main(void)
{
    /* PAL init: enables DWT-equivalent, clk config, NVS flash check */
    pal_init();

    /* I2C buses */
    pal_i2c_init(0, PIN_I2C0_SDA, PIN_I2C0_SCL, 400000);
    pal_i2c_init(1, PIN_I2C1_SDA, PIN_I2C1_SCL, 400000);

    /* SPI bus 0 */
    pal_spi_init(0, PIN_SPI0_SCK, PIN_SPI0_MOSI, PIN_SPI0_MISO, 8000000, 0);

    /* UART 0 — GPS / serial sensors */
    pal_uart_init(0, PIN_UART0_TX, PIN_UART0_RX, 9600);

    /* Status LED */
    pal_gpio_set_direction(PIN_STATUS_LED, PAL_GPIO_OUTPUT, false, false);
    pal_gpio_set_level(PIN_STATUS_LED, 1);

    /* Firmware subsystems */
    state_store_init();
    event_bus_init();
    driver_registry_init();
    watchdog_init();
    scheduler_init();
    vm_init();

    PAL_LOGI("MAIN", "Parakram RP2040 v1.0 ready — %u MHz",
             pal_get_cpu_freq_hz() / 1000000);

    /* Start comms on core 1 */
    multicore_launch_core1(core1_comms_task);

    /* Core 0: VM + scheduler main loop */
    while (1) {
        scheduler_tick();
        vm_tick();
        pal_feed_watchdog();
    }
}
