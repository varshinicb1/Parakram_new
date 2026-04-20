idf_component_register(
    SRCS
        "app_main.c"
        "ota_manager.c"
        "device_config.c"

        # ── Runtime ───────────────────────────────────────────────────────
        "runtime/vm.c"
        "runtime/scheduler.c"
        "runtime/event_bus.c"
        "runtime/state_store.c"

        # ── Driver registry ───────────────────────────────────────────────
        "drivers/driver_registry.c"
        "drivers/i2c_bus.c"
        "drivers/gpio_hal.c"
        "drivers/lvgl_hal.c"

        # ── Sensors (I2C) ─────────────────────────────────────────────────
        "drivers/drv_bme280.c"
        "drivers/drv_bmp280.c"
        "drivers/drv_mpu6050.c"
        "drivers/drv_sht31.c"
        "drivers/drv_bh1750.c"
        "drivers/drv_aht20.c"
        "drivers/drv_max30102.c"
        "drivers/drv_ina219.c"
        "drivers/drv_adxl345.c"
        "drivers/drv_tsl2561.c"
        "drivers/drv_mcp9808.c"
        "drivers/drv_si7021.c"
        "drivers/drv_hts221.c"
        "drivers/drv_vl53l0x.c"
        "drivers/drv_lps22hb.c"
        "drivers/drv_lis3dh.c"
        "drivers/drv_apds9960.c"
        "drivers/drv_ccs811.c"
        "drivers/drv_ens160.c"
        "drivers/drv_sgp30.c"
        "drivers/drv_scd40.c"
        "drivers/drv_veml7700.c"
        "drivers/drv_mlx90614.c"
        "drivers/drv_mfrc522.c"

        # ── Sensors (GPIO / ADC / One-Wire) ───────────────────────────────
        "drivers/drv_dht22.c"
        "drivers/drv_ds18b20.c"
        "drivers/drv_hcsr04.c"
        "drivers/drv_pir.c"
        "drivers/drv_soil_cap.c"
        "drivers/drv_mq2.c"
        "drivers/drv_hx711.c"
        "drivers/drv_reed.c"
        "drivers/drv_rain.c"
        "drivers/drv_tds.c"
        "drivers/drv_ph.c"
        "drivers/drv_yf_s201.c"
        "drivers/drv_max6675.c"

        # ── GPS / UART ────────────────────────────────────────────────────
        "drivers/drv_neo6m.c"

        # ── Actuators ─────────────────────────────────────────────────────
        "drivers/drv_relay.c"
        "drivers/drv_servo.c"
        "drivers/drv_buzzer.c"
        "drivers/drv_motor_dc.c"
        "drivers/drv_motor_stepper.c"
        "drivers/drv_solenoid.c"
        "drivers/drv_fan_pwm.c"
        "drivers/drv_mosfet.c"
        "drivers/drv_ws2812.c"
        "drivers/drv_tb6612.c"
        "drivers/drv_drv8833.c"

        # ── Displays ──────────────────────────────────────────────────────
        "drivers/drv_lcd_i2c.c"
        "drivers/drv_oled_ssd1306.c"
        "drivers/drv_sh1106.c"
        "drivers/drv_ili9341.c"
        "drivers/drv_st7789.c"

        # ── Touch controllers ─────────────────────────────────────────────
        "drivers/drv_cst816s.c"
        "drivers/drv_ft6236.c"

        # ── Audio ─────────────────────────────────────────────────────────
        "drivers/drv_inmp441.c"
        "drivers/drv_max98357a.c"

        # ── Camera ────────────────────────────────────────────────────────
        "drivers/drv_ov2640.c"

        # ── HAL ───────────────────────────────────────────────────────────
        "../hal/esp32s3/pal_impl.c"
        "../hal/common/pal_onewire.c"

        # ── Comms ─────────────────────────────────────────────────────────
        "comms/wifi_mgr.c"
        "comms/ble_gatt_profile.c"
        "comms/mqtt_client.c"

        # ── Security ──────────────────────────────────────────────────────
        "security/payload_verify.c"
        "security/device_identity.c"

        # ── Safety ────────────────────────────────────────────────────────
        "safety/watchdog.c"
        "safety/rate_limiter.c"
        "safety/fault_handler.c"

    INCLUDE_DIRS
        "include"
        "../hal/include"
        "../components/lvgl"
        "../components/lvgl/src"

    REQUIRES
        driver esp_wifi esp_event nvs_flash esp_timer lvgl
        bt mbedtls mqtt esp_adc fatfs
        esp_http_client app_update
)
