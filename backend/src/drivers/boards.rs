//! Board descriptor definitions.

use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BoardDescriptor {
    pub sku: String,
    pub name: String,
    pub soc: String,
    pub flash_mb: u32,
    pub psram_mb: u32,
    pub available_buses: HashMap<String, BusConfig>,
    pub available_slots: HashMap<String, SlotConfig>,
    pub connected_sensors: Vec<String>,
    pub connected_actuators: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BusConfig {
    #[serde(default)]
    pub sda: Option<u8>,
    #[serde(default)]
    pub scl: Option<u8>,
    #[serde(default)]
    pub freq: Option<u32>,
    #[serde(default)]
    pub mosi: Option<u8>,
    #[serde(default)]
    pub miso: Option<u8>,
    #[serde(default)]
    pub sclk: Option<u8>,
    #[serde(default)]
    pub pin: Option<u8>,
    #[serde(default)]
    pub tx: Option<u8>,
    #[serde(default)]
    pub rx: Option<u8>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SlotConfig {
    pub pin: u8,
}

/// Get the default board descriptor for VDYT-S3-R1.
pub fn get_board_vdyt_s3_r1() -> BoardDescriptor {
    let mut buses = HashMap::new();
    buses.insert("i2c_0".into(), BusConfig { sda: Some(8), scl: Some(9), freq: Some(400000), ..Default::default() });
    buses.insert("i2c_1".into(), BusConfig { sda: Some(3), scl: Some(46), freq: Some(400000), ..Default::default() });
    buses.insert("spi_2".into(), BusConfig { mosi: Some(11), miso: Some(13), sclk: Some(12), ..Default::default() });
    buses.insert("onewire_0".into(), BusConfig { pin: Some(21), ..Default::default() });
    buses.insert("uart_1".into(), BusConfig { tx: Some(17), rx: Some(18), ..Default::default() });

    let mut slots = HashMap::new();
    for (name, pin) in [("adc_ch0", 1), ("adc_ch1", 2), ("adc_ch2", 4), ("adc_ch3", 5),
                         ("pwm_ch0", 6), ("pwm_ch1", 7), ("pwm_ch2", 15), ("pwm_ch3", 16),
                         ("gpio_out0", 38), ("gpio_out1", 39), ("gpio_out2", 40), ("gpio_out3", 41),
                         ("gpio_in0", 42), ("gpio_in1", 45), ("gpio_in2", 47), ("gpio_in3", 48)] {
        slots.insert(name.to_string(), SlotConfig { pin });
    }
    slots.insert("ws2812".into(), SlotConfig { pin: 35 });

    BoardDescriptor {
        sku: "VDYT-S3-R1".into(),
        name: "Vidyuthlabs ESP32-S3 Dev Board Rev 1".into(),
        soc: "ESP32-S3".into(),
        flash_mb: 16,
        psram_mb: 8,
        available_buses: buses,
        available_slots: slots,
        connected_sensors: vec![],
        connected_actuators: vec![],
    }
}

impl Default for BusConfig {
    fn default() -> Self {
        Self { sda: None, scl: None, freq: None, mosi: None, miso: None, sclk: None, pin: None, tx: None, rx: None }
    }
}
