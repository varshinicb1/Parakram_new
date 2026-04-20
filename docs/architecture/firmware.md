# Firmware Architecture

The Parakram firmware runs on ESP32-S3 microcontrollers using ESP-IDF v5.1 (FreeRTOS).

## Core Components

### Bytecode Virtual Machine (`vm.c`)
The heart of the firmware. A stack-based interpreter that executes 8-byte instructions at native speed. Zero dynamic allocation in the hot path.

### Driver Registry (`driver_registry.c`)
Maps bytecode driver indices to physical hardware. Currently supports **63 production drivers** covering sensors, actuators, displays, motors, and communication peripherals.

### LVGL HAL (`lvgl_hal.c`)
Hardware Abstraction Layer bridging the official LVGL 8.3 graphics library to ESP-IDF's SPI display pipeline. Supports ST7789, ILI9341, and SSD1306 displays with DMA-accelerated rendering.

### Communication Stack
- **WiFi** (`wifi_mgr.c`) — Station mode with auto-reconnect
- **BLE** (`ble_gatt_profile.c`) — GATT server for mobile app communication
- **MQTT** (`mqtt_client.c`) — Pub/sub for cloud telemetry

### OTA Manager (`ota_manager.c`)
Over-the-air firmware updates using ESP-IDF's native `esp_ota_ops`. SHA-256 verification before committing new partitions.

## Supported Drivers (63)

| Category | Drivers |
|----------|---------|
| Temperature | DHT22, DS18B20, BME280, BMP280, SHT31, AHT20, MCP9808, HTS221, MLX90614, MAX6675, SI7021 |
| Light | BH1750, TSL2561, VEML7700, APDS9960 |
| Motion | MPU6050, ADXL345, LIS3DH |
| Gas/Air | MQ2, CCS811, SGP30, SCD40, ENS160 |
| Distance | HC-SR04, VL53L0X |
| Power | INA219 |
| Weight | HX711 |
| Water | Soil capacitive, pH, Rain, TDS, YF-S201 flow |
| Display | SSD1306 OLED, SH1106, ILI9341, ST7789, LCD I2C |
| Touch | FT6236, CST816S |
| Motor | DC, Stepper, Servo, DRV8833, TB6612 |
| Output | Relay, Buzzer, WS2812 LED, Solenoid, MOSFET, Fan PWM |
| Comms | NEO-6M GPS |
| Security | PIR, Reed switch, MFRC522 RFID |
| Audio | INMP441 mic, MAX98357A speaker |
| Camera | OV2640 |
