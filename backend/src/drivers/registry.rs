//! Parakram Driver Registry
//!
//! In-memory registry of all Vidyuthlabs-supported drivers.
//! Each driver has: name, capabilities, bus types, timing constraints, failure modes.

use std::collections::HashMap;
use serde::{Deserialize, Serialize};

/// Specification of a single driver from the Vidyuthlabs registry.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DriverSpec {
    pub name: String,
    pub display_name: String,
    pub version: String,
    pub driver_type: String, // "sensor", "actuator", "display", "combo"
    pub bus_types: Vec<String>,
    pub capabilities: Vec<String>,
    pub max_latency_us: u32,
    pub min_interval_ms: u32,
    pub i2c_addresses: Vec<String>,
    pub failure_modes: Vec<FailureMode>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FailureMode {
    pub error: String,
    pub description: String,
}

/// In-memory driver registry, populated at startup.
pub struct DriverRegistry {
    drivers: HashMap<String, DriverSpec>,
}

impl DriverRegistry {
    /// Create a new registry pre-populated with all Vidyuthlabs drivers.
    pub fn new() -> Self {
        let mut registry = Self {
            drivers: HashMap::new(),
        };
        registry.populate();
        registry
    }

    pub fn get_driver(&self, name: &str) -> Option<&DriverSpec> {
        self.drivers.get(name)
    }

    pub fn list_all(&self) -> Vec<&DriverSpec> {
        self.drivers.values().collect()
    }

    pub fn list_by_type(&self, driver_type: &str) -> Vec<&DriverSpec> {
        self.drivers.values().filter(|d| d.driver_type == driver_type).collect()
    }

    pub fn count(&self) -> usize {
        self.drivers.len()
    }

    fn add(&mut self, spec: DriverSpec) {
        self.drivers.insert(spec.name.clone(), spec);
    }

    /// Populate registry with all 41 drivers.
    fn populate(&mut self) {
        // ====== SENSORS ======

        self.add(DriverSpec {
            name: "drv_bme280".into(), display_name: "BME280 Environment Sensor".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["temperature".into(), "humidity".into(), "pressure".into(), "altitude".into()],
            max_latency_us: 2000, min_interval_ms: 500,
            i2c_addresses: vec!["0x76".into(), "0x77".into()],
            failure_modes: vec![
                FailureMode { error: "BUS_FAIL".into(), description: "I2C communication failure".into() },
            ],
        });

        self.add(DriverSpec {
            name: "drv_bmp280".into(), display_name: "BMP280 Pressure Sensor".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["temperature".into(), "pressure".into(), "altitude".into()],
            max_latency_us: 2000, min_interval_ms: 500,
            i2c_addresses: vec!["0x76".into(), "0x77".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_dht22".into(), display_name: "DHT22 Temperature & Humidity".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["onewire_0".into(), "onewire".into()],
            capabilities: vec!["temperature".into(), "humidity".into()],
            max_latency_us: 5000, min_interval_ms: 2000,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "TIMEOUT".into(), description: "1-Wire timeout".into() }],
        });

        self.add(DriverSpec {
            name: "drv_ds18b20".into(), display_name: "DS18B20 Temperature".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["onewire_0".into(), "onewire".into()],
            capabilities: vec!["temperature".into()],
            max_latency_us: 750000, min_interval_ms: 1000,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "CRC".into(), description: "CRC mismatch".into() }],
        });

        self.add(DriverSpec {
            name: "drv_sht31".into(), display_name: "SHT31 Temperature & Humidity".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["temperature".into(), "humidity".into()],
            max_latency_us: 2000, min_interval_ms: 500,
            i2c_addresses: vec!["0x44".into(), "0x45".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_si7021".into(), display_name: "SI7021 Temperature & Humidity".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["temperature".into(), "humidity".into()],
            max_latency_us: 3000, min_interval_ms: 500,
            i2c_addresses: vec!["0x40".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_hts221".into(), display_name: "HTS221 Temperature & Humidity".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["temperature".into(), "humidity".into()],
            max_latency_us: 2000, min_interval_ms: 1000,
            i2c_addresses: vec!["0x5F".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_mpu6050".into(), display_name: "MPU6050 6-Axis IMU".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["acceleration_x".into(), "acceleration_y".into(), "acceleration_z".into(),
                             "gyroscope_x".into(), "gyroscope_y".into(), "gyroscope_z".into()],
            max_latency_us: 1000, min_interval_ms: 10,
            i2c_addresses: vec!["0x68".into(), "0x69".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_adxl345".into(), display_name: "ADXL345 Accelerometer".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into(), "spi_2".into()],
            capabilities: vec!["acceleration_x".into(), "acceleration_y".into(), "acceleration_z".into()],
            max_latency_us: 1000, min_interval_ms: 10,
            i2c_addresses: vec!["0x53".into(), "0x1D".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "Bus failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_lis3dh".into(), display_name: "LIS3DH Accelerometer".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["acceleration_x".into(), "acceleration_y".into(), "acceleration_z".into()],
            max_latency_us: 1000, min_interval_ms: 10,
            i2c_addresses: vec!["0x18".into(), "0x19".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_hcsr04".into(), display_name: "HC-SR04 Ultrasonic Distance".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["gpio".into()],
            capabilities: vec!["distance".into()],
            max_latency_us: 30000, min_interval_ms: 60,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "TIMEOUT".into(), description: "Echo timeout".into() }],
        });

        self.add(DriverSpec {
            name: "drv_vl53l0x".into(), display_name: "VL53L0X Laser Distance".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["distance".into()],
            max_latency_us: 30000, min_interval_ms: 50,
            i2c_addresses: vec!["0x29".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_bh1750".into(), display_name: "BH1750 Light Sensor".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["light_lux".into()],
            max_latency_us: 200000, min_interval_ms: 200,
            i2c_addresses: vec!["0x23".into(), "0x5C".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_tsl2561".into(), display_name: "TSL2561 Light Sensor".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["light_lux".into()],
            max_latency_us: 500000, min_interval_ms: 500,
            i2c_addresses: vec!["0x29".into(), "0x39".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_veml7700".into(), display_name: "VEML7700 Ambient Light".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["light_lux".into()],
            max_latency_us: 5000, min_interval_ms: 100,
            i2c_addresses: vec!["0x10".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_apds9960".into(), display_name: "APDS9960 Gesture/Proximity/RGB".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["proximity".into(), "gesture".into(), "color_r".into(), "color_g".into(), "color_b".into(), "light_lux".into()],
            max_latency_us: 30000, min_interval_ms: 100,
            i2c_addresses: vec!["0x39".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_mq2".into(), display_name: "MQ-2 Gas/Smoke Sensor".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["adc".into()],
            capabilities: vec!["gas_ppm".into(), "smoke_ppm".into()],
            max_latency_us: 100, min_interval_ms: 1000,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "OUT_OF_RANGE".into(), description: "ADC saturation".into() }],
        });

        self.add(DriverSpec {
            name: "drv_ccs811".into(), display_name: "CCS811 Air Quality".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["co2_ppm".into(), "tvoc_ppb".into()],
            max_latency_us: 2000, min_interval_ms: 1000,
            i2c_addresses: vec!["0x5A".into(), "0x5B".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_ens160".into(), display_name: "ENS160 Air Quality".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["co2_ppm".into(), "tvoc_ppb".into()],
            max_latency_us: 2000, min_interval_ms: 1000,
            i2c_addresses: vec!["0x52".into(), "0x53".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_ina219".into(), display_name: "INA219 Current/Voltage".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["voltage".into(), "current".into(), "power".into()],
            max_latency_us: 1000, min_interval_ms: 100,
            i2c_addresses: vec!["0x40".into(), "0x41".into(), "0x44".into(), "0x45".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_max30102".into(), display_name: "MAX30102 Pulse Oximeter".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["heart_rate".into(), "spo2".into()],
            max_latency_us: 5000, min_interval_ms: 100,
            i2c_addresses: vec!["0x57".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_mlx90614".into(), display_name: "MLX90614 IR Temperature".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["temperature".into()],
            max_latency_us: 2000, min_interval_ms: 250,
            i2c_addresses: vec!["0x5A".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_max6675".into(), display_name: "MAX6675 Thermocouple".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["spi_2".into(), "spi".into()],
            capabilities: vec!["temperature".into()],
            max_latency_us: 1000, min_interval_ms: 250,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "SPI failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_lps22hb".into(), display_name: "LPS22HB Barometric Pressure".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["pressure".into(), "temperature".into()],
            max_latency_us: 2000, min_interval_ms: 100,
            i2c_addresses: vec!["0x5C".into(), "0x5D".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_hx711".into(), display_name: "HX711 Load Cell".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["gpio".into()],
            capabilities: vec!["weight".into()],
            max_latency_us: 100000, min_interval_ms: 100,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "TIMEOUT".into(), description: "HX711 not ready".into() }],
        });

        self.add(DriverSpec {
            name: "drv_pir".into(), display_name: "PIR Motion Sensor".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["gpio".into()],
            capabilities: vec!["motion".into()],
            max_latency_us: 10, min_interval_ms: 100,
            i2c_addresses: vec![],
            failure_modes: vec![],
        });

        self.add(DriverSpec {
            name: "drv_reed".into(), display_name: "Reed Switch".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["gpio".into()],
            capabilities: vec!["door_state".into()],
            max_latency_us: 10, min_interval_ms: 50,
            i2c_addresses: vec![],
            failure_modes: vec![],
        });

        self.add(DriverSpec {
            name: "drv_soil_cap".into(), display_name: "Capacitive Soil Moisture".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["adc".into()],
            capabilities: vec!["soil_moisture".into()],
            max_latency_us: 100, min_interval_ms: 1000,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "OUT_OF_RANGE".into(), description: "ADC saturation".into() }],
        });

        self.add(DriverSpec {
            name: "drv_rain".into(), display_name: "Rain Sensor".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["adc".into()],
            capabilities: vec!["rain_level".into()],
            max_latency_us: 100, min_interval_ms: 1000,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "OUT_OF_RANGE".into(), description: "ADC saturation".into() }],
        });

        self.add(DriverSpec {
            name: "drv_tds".into(), display_name: "TDS Water Quality".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["adc".into()],
            capabilities: vec!["tds_ppm".into()],
            max_latency_us: 100, min_interval_ms: 1000,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "OUT_OF_RANGE".into(), description: "ADC saturation".into() }],
        });

        self.add(DriverSpec {
            name: "drv_ph".into(), display_name: "pH Sensor".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["adc".into()],
            capabilities: vec!["ph_level".into()],
            max_latency_us: 100, min_interval_ms: 1000,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "OUT_OF_RANGE".into(), description: "ADC saturation".into() }],
        });

        // ====== ACTUATORS ======

        self.add(DriverSpec {
            name: "drv_relay".into(), display_name: "Relay Switch".into(),
            version: "1.0.0".into(), driver_type: "actuator".into(),
            bus_types: vec!["gpio".into()],
            capabilities: vec!["on_off".into()],
            max_latency_us: 10, min_interval_ms: 100,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "HW_FAULT".into(), description: "GPIO stuck".into() }],
        });

        self.add(DriverSpec {
            name: "drv_servo".into(), display_name: "Servo Motor".into(),
            version: "1.0.0".into(), driver_type: "actuator".into(),
            bus_types: vec!["pwm".into()],
            capabilities: vec!["angle_degrees".into()],
            max_latency_us: 100, min_interval_ms: 20,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "HW_FAULT".into(), description: "PWM failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_ws2812".into(), display_name: "WS2812 LED Strip".into(),
            version: "1.0.0".into(), driver_type: "actuator".into(),
            bus_types: vec!["rmt".into()],
            capabilities: vec!["color_rgb".into()],
            max_latency_us: 5000, min_interval_ms: 16,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "HW_FAULT".into(), description: "RMT failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_buzzer".into(), display_name: "Buzzer".into(),
            version: "1.0.0".into(), driver_type: "actuator".into(),
            bus_types: vec!["pwm".into()],
            capabilities: vec!["tone_hz".into(), "on_off".into()],
            max_latency_us: 100, min_interval_ms: 50,
            i2c_addresses: vec![],
            failure_modes: vec![],
        });

        self.add(DriverSpec {
            name: "drv_motor_dc".into(), display_name: "DC Motor".into(),
            version: "1.0.0".into(), driver_type: "actuator".into(),
            bus_types: vec!["pwm".into(), "gpio".into()],
            capabilities: vec!["speed_percent".into(), "direction".into(), "on_off".into()],
            max_latency_us: 100, min_interval_ms: 50,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "HW_FAULT".into(), description: "Motor driver fault".into() }],
        });

        self.add(DriverSpec {
            name: "drv_motor_stepper".into(), display_name: "Stepper Motor".into(),
            version: "1.0.0".into(), driver_type: "actuator".into(),
            bus_types: vec!["gpio".into()],
            capabilities: vec!["angle_degrees".into(), "direction".into()],
            max_latency_us: 1000, min_interval_ms: 10,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "HW_FAULT".into(), description: "Stepper stall".into() }],
        });

        self.add(DriverSpec {
            name: "drv_lcd_i2c".into(), display_name: "LCD I2C Display".into(),
            version: "1.0.0".into(), driver_type: "display".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["text_display".into()],
            max_latency_us: 5000, min_interval_ms: 100,
            i2c_addresses: vec!["0x27".into(), "0x3F".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_oled_ssd1306".into(), display_name: "OLED SSD1306 Display".into(),
            version: "1.0.0".into(), driver_type: "display".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["pixel_display".into(), "text_display".into()],
            max_latency_us: 5000, min_interval_ms: 16,
            i2c_addresses: vec!["0x3C".into(), "0x3D".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_solenoid".into(), display_name: "Solenoid Valve".into(),
            version: "1.0.0".into(), driver_type: "actuator".into(),
            bus_types: vec!["gpio".into()],
            capabilities: vec!["on_off".into(), "flow_control".into()],
            max_latency_us: 10, min_interval_ms: 500,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "HW_FAULT".into(), description: "GPIO stuck".into() }],
        });

        self.add(DriverSpec {
            name: "drv_fan_pwm".into(), display_name: "PWM Fan".into(),
            version: "1.0.0".into(), driver_type: "actuator".into(),
            bus_types: vec!["pwm".into()],
            capabilities: vec!["speed_percent".into(), "on_off".into()],
            max_latency_us: 100, min_interval_ms: 100,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "HW_FAULT".into(), description: "PWM failure".into() }],
        });

        // ====== AUDIO ======

        self.add(DriverSpec {
            name: "drv_inmp441".into(), display_name: "INMP441 MEMS Microphone".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2s_0".into(), "i2s".into()],
            capabilities: vec!["audio_level_db".into(), "audio_stream".into(), "voice_detect".into()],
            max_latency_us: 500, min_interval_ms: 10,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "I2S_FAIL".into(), description: "I2S bus error".into() }],
        });

        self.add(DriverSpec {
            name: "drv_max98357a".into(), display_name: "MAX98357A I2S Amplifier".into(),
            version: "1.0.0".into(), driver_type: "actuator".into(),
            bus_types: vec!["i2s_0".into(), "i2s".into()],
            capabilities: vec!["audio_play".into(), "tone_hz".into(), "volume_percent".into()],
            max_latency_us: 500, min_interval_ms: 10,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "I2S_FAIL".into(), description: "I2S bus error".into() }],
        });

        // ====== TFT DISPLAYS ======

        self.add(DriverSpec {
            name: "drv_st7789".into(), display_name: "ST7789 TFT Display".into(),
            version: "1.0.0".into(), driver_type: "display".into(),
            bus_types: vec!["spi_2".into(), "spi".into()],
            capabilities: vec!["pixel_display".into(), "text_display".into(), "color_rgb".into(), "lvgl_compatible".into()],
            max_latency_us: 2000, min_interval_ms: 16,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "SPI_FAIL".into(), description: "SPI bus error".into() }],
        });

        self.add(DriverSpec {
            name: "drv_ili9341".into(), display_name: "ILI9341 TFT Display".into(),
            version: "1.0.0".into(), driver_type: "display".into(),
            bus_types: vec!["spi_2".into(), "spi".into()],
            capabilities: vec!["pixel_display".into(), "text_display".into(), "color_rgb".into(), "lvgl_compatible".into()],
            max_latency_us: 2000, min_interval_ms: 16,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "SPI_FAIL".into(), description: "SPI bus error".into() }],
        });

        self.add(DriverSpec {
            name: "drv_sh1106".into(), display_name: "SH1106 OLED Display".into(),
            version: "1.0.0".into(), driver_type: "display".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["pixel_display".into(), "text_display".into()],
            max_latency_us: 5000, min_interval_ms: 16,
            i2c_addresses: vec!["0x3C".into(), "0x3D".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        // ====== TOUCH ======

        self.add(DriverSpec {
            name: "drv_cst816s".into(), display_name: "CST816S Capacitive Touch".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["touch_x".into(), "touch_y".into(), "touch_gesture".into(), "touch_pressed".into()],
            max_latency_us: 1000, min_interval_ms: 10,
            i2c_addresses: vec!["0x15".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_ft6236".into(), display_name: "FT6236 Capacitive Touch".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["touch_x".into(), "touch_y".into(), "touch_pressed".into()],
            max_latency_us: 1000, min_interval_ms: 10,
            i2c_addresses: vec!["0x38".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        // ====== CAMERA ======

        self.add(DriverSpec {
            name: "drv_ov2640".into(), display_name: "OV2640 Camera Module".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["camera".into(), "dvp".into()],
            capabilities: vec!["image_capture".into(), "video_stream".into(), "qr_scan".into()],
            max_latency_us: 50000, min_interval_ms: 33,
            i2c_addresses: vec!["0x30".into()],
            failure_modes: vec![FailureMode { error: "CAM_FAIL".into(), description: "Camera init failure".into() }],
        });

        // ====== GPS ======

        self.add(DriverSpec {
            name: "drv_neo6m".into(), display_name: "NEO-6M GPS Module".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["uart_1".into(), "uart".into()],
            capabilities: vec!["latitude".into(), "longitude".into(), "altitude".into(), "speed_knots".into(), "satellites".into()],
            max_latency_us: 100000, min_interval_ms: 1000,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "NO_FIX".into(), description: "GPS no satellite fix".into() }],
        });

        // ====== RFID ======

        self.add(DriverSpec {
            name: "drv_mfrc522".into(), display_name: "MFRC522 RFID Reader".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["spi_2".into(), "spi".into()],
            capabilities: vec!["rfid_uid".into(), "rfid_present".into()],
            max_latency_us: 5000, min_interval_ms: 100,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "SPI_FAIL".into(), description: "SPI bus error".into() }],
        });

        // ====== ADVANCED ENVIRONMENTAL ======

        self.add(DriverSpec {
            name: "drv_sgp30".into(), display_name: "SGP30 VOC & CO2 Sensor".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["co2_ppm".into(), "tvoc_ppb".into()],
            max_latency_us: 12000, min_interval_ms: 1000,
            i2c_addresses: vec!["0x58".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_scd40".into(), display_name: "SCD40 True CO2 Sensor".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["co2_ppm".into(), "temperature".into(), "humidity".into()],
            max_latency_us: 5000, min_interval_ms: 5000,
            i2c_addresses: vec!["0x62".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_aht20".into(), display_name: "AHT20 Temperature & Humidity".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["temperature".into(), "humidity".into()],
            max_latency_us: 80000, min_interval_ms: 2000,
            i2c_addresses: vec!["0x38".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        self.add(DriverSpec {
            name: "drv_mcp9808".into(), display_name: "MCP9808 Precision Temperature".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["i2c_0".into(), "i2c_1".into(), "i2c".into()],
            capabilities: vec!["temperature".into()],
            max_latency_us: 250000, min_interval_ms: 250,
            i2c_addresses: vec!["0x18".into(), "0x19".into(), "0x1A".into(), "0x1B".into()],
            failure_modes: vec![FailureMode { error: "BUS_FAIL".into(), description: "I2C failure".into() }],
        });

        // ====== MOTOR CONTROLLERS ======

        self.add(DriverSpec {
            name: "drv_drv8833".into(), display_name: "DRV8833 Dual Motor Driver".into(),
            version: "1.0.0".into(), driver_type: "actuator".into(),
            bus_types: vec!["pwm".into(), "gpio".into()],
            capabilities: vec!["speed_percent".into(), "direction".into(), "on_off".into(), "dual_channel".into()],
            max_latency_us: 100, min_interval_ms: 10,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "HW_FAULT".into(), description: "Motor driver fault".into() }],
        });

        self.add(DriverSpec {
            name: "drv_tb6612".into(), display_name: "TB6612FNG Motor Driver".into(),
            version: "1.0.0".into(), driver_type: "actuator".into(),
            bus_types: vec!["pwm".into(), "gpio".into()],
            capabilities: vec!["speed_percent".into(), "direction".into(), "on_off".into(), "dual_channel".into()],
            max_latency_us: 100, min_interval_ms: 10,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "HW_FAULT".into(), description: "Motor driver fault".into() }],
        });

        // ====== POWER ======

        self.add(DriverSpec {
            name: "drv_mosfet".into(), display_name: "MOSFET Power Switch".into(),
            version: "1.0.0".into(), driver_type: "actuator".into(),
            bus_types: vec!["gpio".into(), "pwm".into()],
            capabilities: vec!["on_off".into(), "power_percent".into()],
            max_latency_us: 10, min_interval_ms: 10,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "HW_FAULT".into(), description: "MOSFET stuck".into() }],
        });

        // ====== FLOW ======

        self.add(DriverSpec {
            name: "drv_yf_s201".into(), display_name: "YF-S201 Water Flow Sensor".into(),
            version: "1.0.0".into(), driver_type: "sensor".into(),
            bus_types: vec!["gpio".into()],
            capabilities: vec!["flow_rate_lpm".into(), "total_volume_l".into()],
            max_latency_us: 1000, min_interval_ms: 1000,
            i2c_addresses: vec![],
            failure_modes: vec![FailureMode { error: "TIMEOUT".into(), description: "No pulse detected".into() }],
        });
    }
}
