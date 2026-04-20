/**
 * @file parakram_arduino.ino
 * @brief Parakram firmware — Arduino entry point.
 *
 * Compatible with: Arduino Uno/Mega (AVR), ESP32 Arduino, RP2040 Arduino,
 * STM32 Arduino (STM32duino), and any board supported by Arduino framework.
 *
 * PAL is provided by hal/arduino/pal_impl.cpp; all 59 drivers compile
 * unchanged by including their .c files via the SRCS list in
 * platformio.ini (PlatformIO) or placing them in the sketch folder.
 */

#include "Arduino.h"

/* PAL header — extern "C" inside, so safe to include from .ino */
extern "C" {
#include "parakram_pal.h"
#include "driver_registry.h"
#include "vm.h"
#include "scheduler.h"
#include "event_bus.h"
#include "state_store.h"
}

/* Board-specific pin assignments */
#if defined(ESP32)
  #define PIN_I2C0_SDA   21
  #define PIN_I2C0_SCL   22
  #define PIN_STATUS_LED  2
#elif defined(ARDUINO_ARCH_RP2040)
  #define PIN_I2C0_SDA    4
  #define PIN_I2C0_SCL    5
  #define PIN_STATUS_LED 25
#else
  /* Uno / Mega / generic */
  #define PIN_I2C0_SDA   A4
  #define PIN_I2C0_SCL   A5
  #define PIN_STATUS_LED 13
#endif

void setup()
{
    pal_init();   /* Wire.begin(), Serial.begin(115200) */

    pal_gpio_set_direction(PIN_STATUS_LED, PAL_GPIO_OUTPUT, false, false);
    pal_gpio_set_level(PIN_STATUS_LED, 1);

    state_store_init();
    event_bus_init();
    driver_registry_init();
    scheduler_init();
    vm_init();

    PAL_LOGI("MAIN", "Parakram Arduino v1.0 ready — %lu MHz",
             (unsigned long)(F_CPU / 1000000UL));
}

void loop()
{
    /* Drain Serial into event_bus so the VM receives bytecode frames */
    while (Serial.available()) {
        event_bus_push_byte((uint8_t)Serial.read());
    }

    scheduler_tick();
    vm_tick();
    pal_feed_watchdog();
}
