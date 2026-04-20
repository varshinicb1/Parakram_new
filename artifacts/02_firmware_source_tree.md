# Artifact 2 — Firmware Source Tree

## Complete Directory Structure

```
firmware/
├── CMakeLists.txt                          # Top-level IDF project CMake
├── sdkconfig.defaults                      # Default sdkconfig values
├── partitions.csv                          # Custom partition table
├── Kconfig.projbuild                       # Project-level Kconfig options
│
├── main/
│   ├── CMakeLists.txt                      # Main component CMake
│   ├── Kconfig                             # Main component Kconfig
│   ├── app_main.c                          # Entry point: system init sequence
│   │
│   ├── runtime/
│   │   ├── vm.h                            # VM public API: init, load_program, execute_pipeline
│   │   ├── vm.c                            # Bytecode fetch-decode-execute loop, stack machine
│   │   ├── vm_opcodes.h                    # Opcode enum + mnemonic strings for debug
│   │   ├── scheduler.h                     # Scheduler API: register_pipeline, trigger dispatch
│   │   ├── scheduler.c                     # Fixed-interval task dispatcher, trigger evaluation
│   │   ├── event_bus.h                     # Event bus API: publish, subscribe, poll
│   │   ├── event_bus.c                     # Static ring-buffer pub/sub, zero-alloc
│   │   ├── state_store.h                   # Typed variable pool API: get, set, reset
│   │   ├── state_store.c                   # Pre-allocated typed variable pool (int/float/bool)
│   │   ├── constant_pool.h                 # Read-only constant pool API
│   │   └── constant_pool.c                 # Flash-mapped constant strings and literals
│   │
│   ├── drivers/
│   │   ├── driver_abi.h                    # Driver vtable definition, types, error codes
│   │   ├── driver_registry.h               # Registry API: lookup by ID, enumerate
│   │   ├── driver_registry.c               # Static dispatch table, populated at compile time
│   │   ├── driver_types.h                  # sensor_value_t, actuator_cmd_t, driver_config_t
│   │   │
│   │   ├── bus/
│   │   │   ├── i2c_bus.h                   # Shared I2C bus API: init, acquire, release
│   │   │   ├── i2c_bus.c                   # I2C master with per-device mutex locking
│   │   │   ├── spi_bus.h                   # Shared SPI bus API
│   │   │   ├── spi_bus.c                   # SPI master with CS pin management
│   │   │   ├── uart_bus.h                  # UART bus API
│   │   │   ├── uart_bus.c                  # UART driver with DMA ring buffer
│   │   │   ├── onewire_bus.h               # 1-Wire bus API (for DHT sensors)
│   │   │   └── onewire_bus.c               # 1-Wire bit-bang with timing-critical ISR
│   │   │
│   │   ├── hal/
│   │   │   ├── adc_hal.h                   # ADC HAL API: configure channel, read
│   │   │   ├── adc_hal.c                   # ESP32-S3 ADC1/ADC2 with calibration
│   │   │   ├── pwm_hal.h                   # PWM HAL API: configure, set duty
│   │   │   ├── pwm_hal.c                   # LEDC-based PWM with fade support
│   │   │   ├── gpio_hal.h                  # GPIO HAL API: configure, read, write, ISR
│   │   │   └── gpio_hal.c                  # GPIO input/output with edge interrupt registration
│   │   │
│   │   ├── sensors/
│   │   │   ├── drv_dht22.h                 # DHT22 driver header
│   │   │   ├── drv_dht22.c                 # DHT22: temperature + humidity via 1-Wire
│   │   │   ├── drv_bme280.h                # BME280 driver header
│   │   │   ├── drv_bme280.c                # BME280: temp + humidity + pressure via I2C
│   │   │   ├── drv_bmp280.h                # BMP280 driver header
│   │   │   ├── drv_bmp280.c                # BMP280: temp + pressure via I2C
│   │   │   ├── drv_mpu6050.h               # MPU6050 driver header
│   │   │   ├── drv_mpu6050.c               # MPU6050: 6-axis IMU via I2C
│   │   │   ├── drv_adxl345.h               # ADXL345 driver header
│   │   │   ├── drv_adxl345.c               # ADXL345: 3-axis accelerometer via I2C/SPI
│   │   │   ├── drv_hcsr04.h                # HC-SR04 driver header
│   │   │   ├── drv_hcsr04.c                # HC-SR04: ultrasonic distance via GPIO trigger/echo
│   │   │   ├── drv_bh1750.h                # BH1750 driver header
│   │   │   ├── drv_bh1750.c                # BH1750: ambient light sensor via I2C
│   │   │   ├── drv_mq2.h                   # MQ-2 driver header
│   │   │   ├── drv_mq2.c                   # MQ-2: gas/smoke sensor via ADC
│   │   │   ├── drv_ds18b20.h               # DS18B20 driver header
│   │   │   ├── drv_ds18b20.c               # DS18B20: temperature via 1-Wire
│   │   │   ├── drv_ina219.h                # INA219 driver header
│   │   │   ├── drv_ina219.c                # INA219: current/voltage sensor via I2C
│   │   │   ├── drv_max30102.h              # MAX30102 driver header
│   │   │   ├── drv_max30102.c              # MAX30102: pulse oximeter via I2C
│   │   │   ├── drv_tsl2561.h               # TSL2561 driver header
│   │   │   ├── drv_tsl2561.c               # TSL2561: light sensor via I2C
│   │   │   ├── drv_si7021.h                # SI7021 driver header
│   │   │   ├── drv_si7021.c                # SI7021: temp + humidity via I2C
│   │   │   ├── drv_veml7700.h              # VEML7700 driver header
│   │   │   ├── drv_veml7700.c              # VEML7700: ambient light via I2C
│   │   │   ├── drv_sht31.h                 # SHT31 driver header
│   │   │   ├── drv_sht31.c                 # SHT31: temp + humidity via I2C
│   │   │   ├── drv_ccs811.h                # CCS811 driver header
│   │   │   ├── drv_ccs811.c                # CCS811: eCO2 + TVOC via I2C
│   │   │   ├── drv_vl53l0x.h               # VL53L0X driver header
│   │   │   ├── drv_vl53l0x.c               # VL53L0X: ToF laser distance via I2C
│   │   │   ├── drv_mlx90614.h              # MLX90614 driver header
│   │   │   ├── drv_mlx90614.c              # MLX90614: IR temperature via I2C
│   │   │   ├── drv_apds9960.h              # APDS9960 driver header
│   │   │   ├── drv_apds9960.c              # APDS9960: gesture + proximity + ALS + RGB via I2C
│   │   │   ├── drv_ens160.h                # ENS160 driver header
│   │   │   ├── drv_ens160.c                # ENS160: air quality via I2C
│   │   │   ├── drv_hts221.h                # HTS221 driver header
│   │   │   ├── drv_hts221.c                # HTS221: temp + humidity via I2C
│   │   │   ├── drv_lps22hb.h               # LPS22HB driver header
│   │   │   ├── drv_lps22hb.c               # LPS22HB: barometric pressure via I2C
│   │   │   ├── drv_lis3dh.h                # LIS3DH driver header
│   │   │   ├── drv_lis3dh.c                # LIS3DH: 3-axis accelerometer via I2C/SPI
│   │   │   ├── drv_max6675.h               # MAX6675 driver header
│   │   │   ├── drv_max6675.c               # MAX6675: thermocouple via SPI
│   │   │   ├── drv_hx711.h                 # HX711 driver header
│   │   │   ├── drv_hx711.c                 # HX711: load cell amplifier via GPIO
│   │   │   ├── drv_pir.h                   # PIR motion driver header
│   │   │   ├── drv_pir.c                   # PIR: motion detection via GPIO interrupt
│   │   │   ├── drv_reed.h                  # Reed switch driver header
│   │   │   ├── drv_reed.c                  # Reed switch: magnetic door sensor via GPIO
│   │   │   ├── drv_soil_cap.h              # Capacitive soil sensor header
│   │   │   ├── drv_soil_cap.c              # Capacitive soil moisture via ADC
│   │   │   ├── drv_rain.h                  # Rain sensor driver header
│   │   │   ├── drv_rain.c                  # Rain sensor: via ADC
│   │   │   ├── drv_tds.h                   # TDS meter driver header
│   │   │   ├── drv_tds.c                   # TDS meter: water quality via ADC
│   │   │   ├── drv_ph.h                    # pH sensor driver header
│   │   │   └── drv_ph.c                    # pH sensor: analog pH via ADC
│   │   │
│   │   └── actuators/
│   │       ├── drv_relay.h                 # Relay driver header
│   │       ├── drv_relay.c                 # Relay: GPIO on/off with debounce
│   │       ├── drv_servo.h                 # Servo driver header
│   │       ├── drv_servo.c                 # Servo: PWM-controlled 0-180°
│   │       ├── drv_ws2812.h                # WS2812 driver header
│   │       ├── drv_ws2812.c                # WS2812: addressable LED strip via RMT
│   │       ├── drv_buzzer.h                # Buzzer driver header
│   │       ├── drv_buzzer.c                # Buzzer: PWM tone generation
│   │       ├── drv_motor_dc.h              # DC motor driver header
│   │       ├── drv_motor_dc.c              # DC motor: PWM speed + GPIO direction via H-bridge
│   │       ├── drv_motor_stepper.h         # Stepper motor driver header
│   │       ├── drv_motor_stepper.c         # Stepper: step/dir control via GPIO
│   │       ├── drv_lcd_i2c.h               # LCD I2C driver header
│   │       ├── drv_lcd_i2c.c               # LCD 16x2/20x4: character display via I2C (PCF8574)
│   │       ├── drv_oled_ssd1306.h          # OLED SSD1306 driver header
│   │       ├── drv_oled_ssd1306.c          # OLED SSD1306: 128x64 display via I2C
│   │       ├── drv_solenoid.h              # Solenoid valve driver header
│   │       ├── drv_solenoid.c              # Solenoid: GPIO on/off with soft-start
│   │       ├── drv_fan_pwm.h               # PWM fan driver header
│   │       └── drv_fan_pwm.c               # PWM fan: speed control via LEDC
│   │
│   ├── comms/
│   │   ├── wifi_mgr.h                      # WiFi manager API: connect, status, reconnect
│   │   ├── wifi_mgr.c                      # STA mode connection FSM with reconnect backoff
│   │   ├── mqtt_client.h                   # MQTT client API: connect, publish, subscribe
│   │   ├── mqtt_client.c                   # MQTT 3.1.1 client over TLS (esp-mqtt)
│   │   ├── ble_mgr.h                       # BLE manager API: start, stop advertising
│   │   ├── ble_mgr.c                       # NimBLE GATT server: config + telemetry + status
│   │   ├── ble_gatt_profile.h              # GATT service/characteristic definitions
│   │   ├── ble_gatt_profile.c              # GATT profile registration and callbacks
│   │   ├── lora_mgr.h                      # LoRa manager API
│   │   ├── lora_mgr.c                      # SX127x via SPI: init, send, receive
│   │   ├── esp_now_mgr.h                   # ESP-NOW manager API
│   │   └── esp_now_mgr.c                   # ESP-NOW peer-to-peer: register peer, send, recv
│   │
│   ├── security/
│   │   ├── payload_verify.h                # Payload verification API
│   │   ├── payload_verify.c                # Ed25519 signature verification (mbedtls)
│   │   ├── secure_storage.h                # Secure storage API: read, write encrypted NVS
│   │   ├── secure_storage.c                # NVS operations with flash encryption
│   │   ├── device_identity.h               # Device identity API: get UUID, pubkey
│   │   └── device_identity.c               # Read device keypair and bound user_id from eFuse/NVS
│   │
│   ├── safety/
│   │   ├── watchdog.h                      # Watchdog API: init, feed, register task
│   │   ├── watchdog.c                      # Two-level: TWDT (per-task) + IWDT (system-level)
│   │   ├── rate_limiter.h                  # Rate limiter API: check, record invocation
│   │   ├── rate_limiter.c                  # Per-driver call frequency enforcement
│   │   ├── fault_handler.h                 # Fault handler API: register, trigger
│   │   └── fault_handler.c                 # Panic capture → NVS log → safe state → reset
│   │
│   ├── storage/
│   │   ├── sd_logger.h                     # SD card logger API: init, write, flush
│   │   ├── sd_logger.c                     # SDMMC/SPI SD card CSV logger with ring buffer
│   │   ├── nvs_store.h                     # NVS abstraction API
│   │   └── nvs_store.c                     # Abstraction over esp_nvs for structured data
│   │
│   └── config/
│       ├── board_config.h                  # Board-specific constants: pin map, bus config
│       ├── board_vdyt_s3_r1.h              # Board SKU VDYT-S3-R1 pin/bus definitions
│       ├── system_config.h                 # System constants: pool sizes, limits, timeouts
│       └── pin_safety_map.h                # Safe pin list, conflict matrix
│
├── components/
│   ├── micro_ecc/                          # Ed25519 implementation (component)
│   │   ├── CMakeLists.txt
│   │   └── (source files)
│   └── cbor_tiny/                          # CBOR encoder for compact telemetry (optional)
│       ├── CMakeLists.txt
│       └── (source files)
│
└── tools/
    ├── gen_driver_registry.py              # Script: generates driver_registry.c from driver headers
    ├── gen_pin_map.py                      # Script: generates board_config.h from board JSON
    └── bytecode_disasm.py                  # Script: disassembles bytecode binary for debugging
```

---

## Top-Level CMakeLists.txt

```cmake
# Parakram Firmware — Top-Level CMakeLists.txt
# Target: ESP32-S3, SDK: ESP-IDF 5.x

cmake_minimum_required(VERSION 3.16)

# Pull in ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set target SoC
set(IDF_TARGET "esp32s3")

# Project name
project(parakram_firmware VERSION 1.0.0)
```

---

## Main Component CMakeLists.txt

```cmake
# firmware/main/CMakeLists.txt

idf_component_register(
    SRCS
        "app_main.c"
        # Runtime
        "runtime/vm.c"
        "runtime/scheduler.c"
        "runtime/event_bus.c"
        "runtime/state_store.c"
        "runtime/constant_pool.c"
        # Drivers — Bus
        "drivers/driver_registry.c"
        "drivers/bus/i2c_bus.c"
        "drivers/bus/spi_bus.c"
        "drivers/bus/uart_bus.c"
        "drivers/bus/onewire_bus.c"
        # Drivers — HAL
        "drivers/hal/adc_hal.c"
        "drivers/hal/pwm_hal.c"
        "drivers/hal/gpio_hal.c"
        # Drivers — Sensors (representative subset, full list in production)
        "drivers/sensors/drv_dht22.c"
        "drivers/sensors/drv_bme280.c"
        "drivers/sensors/drv_bmp280.c"
        "drivers/sensors/drv_mpu6050.c"
        "drivers/sensors/drv_adxl345.c"
        "drivers/sensors/drv_hcsr04.c"
        "drivers/sensors/drv_bh1750.c"
        "drivers/sensors/drv_mq2.c"
        "drivers/sensors/drv_ds18b20.c"
        "drivers/sensors/drv_ina219.c"
        "drivers/sensors/drv_max30102.c"
        "drivers/sensors/drv_tsl2561.c"
        "drivers/sensors/drv_si7021.c"
        "drivers/sensors/drv_veml7700.c"
        "drivers/sensors/drv_sht31.c"
        "drivers/sensors/drv_ccs811.c"
        "drivers/sensors/drv_vl53l0x.c"
        "drivers/sensors/drv_mlx90614.c"
        "drivers/sensors/drv_apds9960.c"
        "drivers/sensors/drv_ens160.c"
        "drivers/sensors/drv_hts221.c"
        "drivers/sensors/drv_lps22hb.c"
        "drivers/sensors/drv_lis3dh.c"
        "drivers/sensors/drv_max6675.c"
        "drivers/sensors/drv_hx711.c"
        "drivers/sensors/drv_pir.c"
        "drivers/sensors/drv_reed.c"
        "drivers/sensors/drv_soil_cap.c"
        "drivers/sensors/drv_rain.c"
        "drivers/sensors/drv_tds.c"
        "drivers/sensors/drv_ph.c"
        # Drivers — Actuators
        "drivers/actuators/drv_relay.c"
        "drivers/actuators/drv_servo.c"
        "drivers/actuators/drv_ws2812.c"
        "drivers/actuators/drv_buzzer.c"
        "drivers/actuators/drv_motor_dc.c"
        "drivers/actuators/drv_motor_stepper.c"
        "drivers/actuators/drv_lcd_i2c.c"
        "drivers/actuators/drv_oled_ssd1306.c"
        "drivers/actuators/drv_solenoid.c"
        "drivers/actuators/drv_fan_pwm.c"
        # Comms
        "comms/wifi_mgr.c"
        "comms/mqtt_client.c"
        "comms/ble_mgr.c"
        "comms/ble_gatt_profile.c"
        "comms/lora_mgr.c"
        "comms/esp_now_mgr.c"
        # Security
        "security/payload_verify.c"
        "security/secure_storage.c"
        "security/device_identity.c"
        # Safety
        "safety/watchdog.c"
        "safety/rate_limiter.c"
        "safety/fault_handler.c"
        # Storage
        "storage/sd_logger.c"
        "storage/nvs_store.c"

    INCLUDE_DIRS
        "."
        "runtime"
        "drivers"
        "drivers/bus"
        "drivers/hal"
        "drivers/sensors"
        "drivers/actuators"
        "comms"
        "security"
        "safety"
        "storage"
        "config"

    REQUIRES
        driver
        esp_wifi
        esp_event
        esp_timer
        nvs_flash
        esp_netif
        mqtt
        bt
        fatfs
        sdmmc
        mbedtls
        esp_adc
        spi_flash
        esp_system

    PRIV_REQUIRES
        micro_ecc
)
```

---

## Partition Table (partitions.csv)

```csv
# Parakram ESP32-S3 Partition Table
# Name,         Type,  SubType,  Offset,    Size,      Flags
nvs,            data,  nvs,      0x9000,    0x6000,
otadata,        data,  ota,      0xf000,    0x2000,
phy_init,       data,  phy,      0x11000,   0x1000,
factory,        app,   factory,  0x20000,   0x200000,
ota_0,          app,   ota_0,    0x220000,  0x200000,
ota_1,          app,   ota_1,    0x420000,  0x200000,
config,         data,  0x40,     0x620000,  0x10000,
program,        data,  0x41,     0x630000,  0x10000,
telemetry,      data,  0x42,     0x640000,  0x40000,
coredump,       data,  coredump, 0x680000,  0x10000,
storage,        data,  fat,      0x690000,  0x170000,
```

### Partition purposes:
| Partition | Size | Purpose |
|-----------|------|---------|
| nvs | 24KB | Non-volatile storage: WiFi credentials, device identity, bound user |
| otadata | 8KB | OTA boot state tracking |
| phy_init | 4KB | RF calibration data |
| factory | 2MB | Factory firmware image |
| ota_0 | 2MB | OTA slot A |
| ota_1 | 2MB | OTA slot B |
| config | 64KB | Active device configuration (board descriptor) |
| program | 64KB | Active bytecode program (signed, 8KB max used, space for growth) |
| telemetry | 256KB | Circular telemetry log buffer |
| coredump | 64KB | Core dump on panic |
| storage | 1.4MB | FAT filesystem for SD card fallback / local logs |

**Total flash usage: ~8.6MB of 16MB** — leaves headroom for future expansion.

---

## Kconfig.projbuild

```kconfig
menu "Parakram Configuration"

    config PARAKRAM_BOARD_SKU
        string "Board SKU"
        default "VDYT-S3-R1"
        help
            Vidyuthlabs board SKU identifier. Determines pin map and default devices.

    config PARAKRAM_MAX_PIPELINES
        int "Maximum number of concurrent pipelines"
        default 16
        range 1 32
        help
            Maximum number of pipelines that can execute concurrently.
            Each pipeline consumes one pre-allocated FreeRTOS task.

    config PARAKRAM_MAX_STATE_VARS
        int "Maximum number of state variables"
        default 64
        range 8 128
        help
            Maximum number of typed state variables in the variable pool.

    config PARAKRAM_MAX_INSTRUCTIONS
        int "Maximum bytecode instructions per program"
        default 1024
        range 256 2048
        help
            Maximum number of 8-byte instructions in a bytecode program.

    config PARAKRAM_VM_STACK_DEPTH
        int "VM operand stack depth"
        default 16
        range 8 32
        help
            Maximum depth of the VM operand stack per pipeline.

    config PARAKRAM_MAX_DEVICES
        int "Maximum registered devices"
        default 32
        range 8 64
        help
            Maximum number of hardware devices (sensors + actuators) supported.

    config PARAKRAM_MAX_EVENTS
        int "Event bus ring buffer capacity"
        default 64
        range 16 256
        help
            Number of event entries in the static event bus ring buffer.

    config PARAKRAM_MAX_CONSTANTS
        int "Maximum constant pool entries"
        default 128
        range 32 256
        help
            Maximum number of entries in the bytecode constant pool.

    config PARAKRAM_TELEMETRY_INTERVAL_MS
        int "Telemetry report interval (ms)"
        default 1000
        range 100 60000
        help
            How often telemetry is reported via BLE/MQTT.

    config PARAKRAM_WATCHDOG_TIMEOUT_S
        int "Task watchdog timeout (seconds)"
        default 10
        range 5 60
        help
            Task watchdog timer timeout. If a task doesn't feed the
            watchdog within this period, fault handler is invoked.

    config PARAKRAM_WIFI_RECONNECT_MAX_S
        int "WiFi max reconnect backoff (seconds)"
        default 300
        range 10 600
        help
            Maximum exponential backoff delay for WiFi reconnection.

    config PARAKRAM_ENABLE_LORA
        bool "Enable LoRa communication"
        default n
        help
            Enable LoRa (SX127x) communication module. Requires LoRa hardware.

    config PARAKRAM_ENABLE_ESP_NOW
        bool "Enable ESP-NOW communication"
        default n
        help
            Enable ESP-NOW peer-to-peer communication.

    config PARAKRAM_ENABLE_SD_LOGGING
        bool "Enable SD card logging"
        default y
        help
            Enable logging telemetry data to SD card via SDMMC or SPI.

    config PARAKRAM_SECURE_BOOT
        bool "Enable Secure Boot V2"
        default y
        help
            Enable ESP32-S3 Secure Boot V2 (RSA-PSS).
            WARNING: Once enabled and eFuses burned, this is irreversible.

    config PARAKRAM_FLASH_ENCRYPTION
        bool "Enable Flash Encryption"
        default y
        help
            Enable AES-XTS 256-bit flash encryption.
            WARNING: Once enabled in RELEASE mode, this is irreversible.

    config PARAKRAM_TCP_CONFIG_PORT
        int "TCP config receive port"
        default 8423
        range 1024 65535
        help
            TCP port for receiving signed bytecode payloads over WiFi.

endmenu
```

---

## sdkconfig.defaults

```ini
# Parakram ESP32-S3 — Default SDK Configuration

# Target
CONFIG_IDF_TARGET="esp32s3"

# Flash
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y

# Partition table
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# PSRAM
CONFIG_ESP32S3_SPIRAM_SUPPORT=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_BOOT_INIT=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y

# FreeRTOS
CONFIG_FREERTOS_HZ=1000
CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH=4096
CONFIG_FREERTOS_IDLE_TASK_STACKSIZE=2048
CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK=y
CONFIG_FREERTOS_ENABLE_STATIC_TASK_CLEAN_UP=y

# WiFi
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=6
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=12
CONFIG_ESP_WIFI_STATIC_TX_BUFFER=y
CONFIG_ESP_WIFI_TX_BUFFER_TYPE=0
CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM=6
CONFIG_ESP_WIFI_AMPDU_TX_ENABLED=y
CONFIG_ESP_WIFI_AMPDU_RX_ENABLED=y

# BLE (NimBLE)
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=517
CONFIG_BT_NIMBLE_ROLE_CENTRAL=n
CONFIG_BT_NIMBLE_ROLE_OBSERVER=n
CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y

# MQTT
CONFIG_MQTT_TRANSPORT_SSL=y
CONFIG_MQTT_BUFFER_SIZE=2048

# mbedTLS (for Ed25519 and TLS)
CONFIG_MBEDTLS_HARDWARE_AES=y
CONFIG_MBEDTLS_HARDWARE_SHA=y
CONFIG_MBEDTLS_ECP_DP_CURVE25519_ENABLED=y

# Watchdog
CONFIG_ESP_TASK_WDT=y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y
CONFIG_ESP_INT_WDT=y

# Core dump
CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y
CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y

# Log level
CONFIG_LOG_DEFAULT_LEVEL_INFO=y

# Secure Boot
CONFIG_SECURE_BOOT=y
CONFIG_SECURE_BOOT_V2_ENABLED=y

# Flash Encryption
CONFIG_SECURE_FLASH_ENC_ENABLED=y
CONFIG_SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT=y

# Power Management
CONFIG_PM_ENABLE=y
CONFIG_PM_DFS_INIT_AUTO=y
```

---

## Board Configuration — VDYT-S3-R1

```c
// board_vdyt_s3_r1.h — Vidyuthlabs ESP32-S3 Rev 1 Pin Map
// Board SKU: VDYT-S3-R1
// Shield: Universal Sensor Shield V1

#ifndef BOARD_VDYT_S3_R1_H
#define BOARD_VDYT_S3_R1_H

// ============================================================
// I2C Bus Configuration
// ============================================================
#define BOARD_I2C0_SDA_PIN      GPIO_NUM_8
#define BOARD_I2C0_SCL_PIN      GPIO_NUM_9
#define BOARD_I2C0_FREQ_HZ      400000

#define BOARD_I2C1_SDA_PIN      GPIO_NUM_3
#define BOARD_I2C1_SCL_PIN      GPIO_NUM_46
#define BOARD_I2C1_FREQ_HZ      400000

// ============================================================
// SPI Bus Configuration
// ============================================================
#define BOARD_SPI2_MOSI_PIN     GPIO_NUM_11
#define BOARD_SPI2_MISO_PIN     GPIO_NUM_13
#define BOARD_SPI2_SCLK_PIN     GPIO_NUM_12
#define BOARD_SPI2_CS0_PIN      GPIO_NUM_10      // SD card
#define BOARD_SPI2_CS1_PIN      GPIO_NUM_14      // LoRa SX127x

// ============================================================
// UART Configuration
// ============================================================
#define BOARD_UART1_TX_PIN      GPIO_NUM_17
#define BOARD_UART1_RX_PIN      GPIO_NUM_18

// ============================================================
// ADC Channels (safe pins only)
// ============================================================
#define BOARD_ADC_CH0_PIN       GPIO_NUM_1       // ADC1_CH0 — analog sensor 1
#define BOARD_ADC_CH1_PIN       GPIO_NUM_2       // ADC1_CH1 — analog sensor 2
#define BOARD_ADC_CH2_PIN       GPIO_NUM_4       // ADC1_CH3 — analog sensor 3
#define BOARD_ADC_CH3_PIN       GPIO_NUM_5       // ADC1_CH4 — analog sensor 4

// ============================================================
// PWM Outputs (safe pins only)
// ============================================================
#define BOARD_PWM_CH0_PIN       GPIO_NUM_6       // Servo / PWM fan
#define BOARD_PWM_CH1_PIN       GPIO_NUM_7       // Buzzer / motor
#define BOARD_PWM_CH2_PIN       GPIO_NUM_15      // LED / additional PWM
#define BOARD_PWM_CH3_PIN       GPIO_NUM_16      // LED / additional PWM

// ============================================================
// GPIO Digital I/O (safe pins only)
// ============================================================
#define BOARD_GPIO_OUT0_PIN     GPIO_NUM_38      // Relay 1
#define BOARD_GPIO_OUT1_PIN     GPIO_NUM_39      // Relay 2
#define BOARD_GPIO_OUT2_PIN     GPIO_NUM_40      // Solenoid / digital actuator
#define BOARD_GPIO_OUT3_PIN     GPIO_NUM_41      // Digital actuator

#define BOARD_GPIO_IN0_PIN      GPIO_NUM_42      // PIR / digital sensor
#define BOARD_GPIO_IN1_PIN      GPIO_NUM_45      // Reed switch / digital sensor
#define BOARD_GPIO_IN2_PIN      GPIO_NUM_47      // HC-SR04 trigger
#define BOARD_GPIO_IN3_PIN      GPIO_NUM_48      // HC-SR04 echo

// ============================================================
// 1-Wire Bus
// ============================================================
#define BOARD_ONEWIRE_PIN       GPIO_NUM_21      // DHT22, DS18B20

// ============================================================
// WS2812 LED Strip (RMT)
// ============================================================
#define BOARD_WS2812_PIN        GPIO_NUM_35
#define BOARD_WS2812_MAX_LEDS   60

// ============================================================
// SD Card (SPI mode)
// ============================================================
#define BOARD_SD_CS_PIN         BOARD_SPI2_CS0_PIN
#define BOARD_SD_MOUNT_POINT    "/sdcard"

// ============================================================
// Status LEDs (on-board)
// ============================================================
#define BOARD_LED_STATUS_PIN    GPIO_NUM_36      // System status LED
#define BOARD_LED_ERROR_PIN     GPIO_NUM_37      // Error indicator LED

// ============================================================
// Boot / Reset
// ============================================================
#define BOARD_BOOT_PIN          GPIO_NUM_0       // BOOT button (reserved)
// GPIO_NUM_19, GPIO_NUM_20 reserved for USB D-/D+

#endif // BOARD_VDYT_S3_R1_H
```

---

## app_main.c — Boot Sequence

```c
// app_main.c — Parakram Firmware Entry Point
// Boot sequence: deterministic, all resources pre-allocated

#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"

#include "board_config.h"
#include "system_config.h"
#include "device_identity.h"
#include "secure_storage.h"
#include "watchdog.h"
#include "fault_handler.h"
#include "i2c_bus.h"
#include "spi_bus.h"
#include "gpio_hal.h"
#include "adc_hal.h"
#include "pwm_hal.h"
#include "driver_registry.h"
#include "event_bus.h"
#include "state_store.h"
#include "constant_pool.h"
#include "vm.h"
#include "scheduler.h"
#include "wifi_mgr.h"
#include "ble_mgr.h"
#include "mqtt_client.h"
#include "payload_verify.h"
#include "sd_logger.h"
#include "rate_limiter.h"

static const char *TAG = "parakram";

void app_main(void)
{
    esp_err_t ret;

    // Phase 1: Core system initialization
    ESP_LOGI(TAG, "=== Parakram Firmware v1.0.0 ===");
    ESP_LOGI(TAG, "Phase 1: Core system init");

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Phase 2: Security initialization
    ESP_LOGI(TAG, "Phase 2: Security init");
    ESP_ERROR_CHECK(device_identity_init());
    ESP_ERROR_CHECK(secure_storage_init());
    ESP_ERROR_CHECK(payload_verify_init());

    // Phase 3: Safety systems
    ESP_LOGI(TAG, "Phase 3: Safety systems init");
    ESP_ERROR_CHECK(fault_handler_init());
    ESP_ERROR_CHECK(watchdog_init());
    ESP_ERROR_CHECK(rate_limiter_init());

    // Phase 4: Hardware bus initialization
    ESP_LOGI(TAG, "Phase 4: Hardware bus init");
    ESP_ERROR_CHECK(i2c_bus_init());
    ESP_ERROR_CHECK(spi_bus_init());
    ESP_ERROR_CHECK(gpio_hal_init());
    ESP_ERROR_CHECK(adc_hal_init());
    ESP_ERROR_CHECK(pwm_hal_init());

    // Phase 5: Driver registry
    ESP_LOGI(TAG, "Phase 5: Driver registry init");
    ESP_ERROR_CHECK(driver_registry_init());

    // Phase 6: Runtime engine
    ESP_LOGI(TAG, "Phase 6: Runtime engine init");
    ESP_ERROR_CHECK(event_bus_init());
    ESP_ERROR_CHECK(state_store_init());
    ESP_ERROR_CHECK(constant_pool_init());
    ESP_ERROR_CHECK(vm_init());
    ESP_ERROR_CHECK(scheduler_init());

    // Phase 7: Communication stacks
    ESP_LOGI(TAG, "Phase 7: Communication init");
    ESP_ERROR_CHECK(ble_mgr_init());    // BLE always available
    ESP_ERROR_CHECK(wifi_mgr_init());   // WiFi connects asynchronously

    #if CONFIG_PARAKRAM_ENABLE_SD_LOGGING
    ret = sd_logger_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD card not available, logging to PSRAM buffer");
    }
    #endif

    // Phase 8: Load active program (if any)
    ESP_LOGI(TAG, "Phase 8: Program load");
    ret = vm_load_program_from_partition();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Active program loaded, starting scheduler");
        ESP_ERROR_CHECK(scheduler_start());
    } else {
        ESP_LOGI(TAG, "No active program, awaiting deployment");
    }

    // Phase 9: Start BLE advertising (wait for app connection)
    ESP_LOGI(TAG, "Phase 9: Start BLE advertising");
    ESP_ERROR_CHECK(ble_mgr_start_advertising());

    ESP_LOGI(TAG, "=== Boot complete, system running ===");

    // Main task feeds watchdog and handles system events
    while (1) {
        watchdog_feed_main();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

---

## system_config.h — Compile-Time Constants

```c
// system_config.h — Parakram System Constants
// All pool sizes and limits defined here. No runtime configuration.

#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include "sdkconfig.h"

// Runtime limits (from Kconfig)
#define SYS_MAX_PIPELINES           CONFIG_PARAKRAM_MAX_PIPELINES
#define SYS_MAX_STATE_VARS          CONFIG_PARAKRAM_MAX_STATE_VARS
#define SYS_MAX_INSTRUCTIONS        CONFIG_PARAKRAM_MAX_INSTRUCTIONS
#define SYS_VM_STACK_DEPTH          CONFIG_PARAKRAM_VM_STACK_DEPTH
#define SYS_MAX_DEVICES             CONFIG_PARAKRAM_MAX_DEVICES
#define SYS_MAX_EVENTS              CONFIG_PARAKRAM_MAX_EVENTS
#define SYS_MAX_CONSTANTS           CONFIG_PARAKRAM_MAX_CONSTANTS

// Memory budgets (SRAM)
#define SYS_STATE_POOL_BYTES        (SYS_MAX_STATE_VARS * 16)       // 1KB
#define SYS_EVENT_RING_BYTES        (SYS_MAX_EVENTS * 16)           // 1KB
#define SYS_CONST_POOL_BYTES        (SYS_MAX_CONSTANTS * 36)        // ~4.5KB
#define SYS_VM_STACK_BYTES          (SYS_VM_STACK_DEPTH * 8 * SYS_MAX_PIPELINES) // 2KB

// Task stack sizes (words, not bytes)
#define SYS_PIPELINE_TASK_STACK     2048
#define SYS_SCHEDULER_TASK_STACK    4096
#define SYS_BLE_TASK_STACK          4096
#define SYS_WIFI_TASK_STACK         4096
#define SYS_MQTT_TASK_STACK         4096

// Task priorities (higher number = higher priority)
#define SYS_PRIORITY_SAFETY         (configMAX_PRIORITIES - 1)  // 24
#define SYS_PRIORITY_SCHEDULER      (configMAX_PRIORITIES - 2)  // 23
#define SYS_PRIORITY_PIPELINE       (configMAX_PRIORITIES - 5)  // 20
#define SYS_PRIORITY_COMMS          (configMAX_PRIORITIES - 8)  // 17
#define SYS_PRIORITY_TELEMETRY      (configMAX_PRIORITIES - 10) // 15

// Timing
#define SYS_MIN_TRIGGER_INTERVAL_MS 100
#define SYS_MAX_PIPELINE_EXEC_MS    5000
#define SYS_WATCHDOG_TIMEOUT_S      CONFIG_PARAKRAM_WATCHDOG_TIMEOUT_S

// String limits
#define SYS_MAX_STRING_LEN          32
#define SYS_MAX_TOPIC_LEN           64
#define SYS_MAX_DEVICE_NAME_LEN     32

// Bytecode format
#define SYS_BYTECODE_MAGIC          0x50524B4D  // "PRKM"
#define SYS_BYTECODE_VERSION        1
#define SYS_INSTRUCTION_SIZE        8       // bytes per instruction

// TCP config port
#define SYS_TCP_CONFIG_PORT         CONFIG_PARAKRAM_TCP_CONFIG_PORT

#endif // SYSTEM_CONFIG_H
```
