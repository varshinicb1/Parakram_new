/**
 * @file drv_lcd_i2c.c
 * @brief HD44780 LCD driver via PCF8574 I2C backpack.
 *
 * PCF8574 bit layout (standard backpack wiring):
 *   Bit 0 – RS   (Register Select: 0=command, 1=data)
 *   Bit 1 – RW   (Read/Write:      0=write, always 0 here)
 *   Bit 2 – EN   (Enable strobe)
 *   Bit 3 – BL   (Backlight:       1=on)
 *   Bit 4 – D4
 *   Bit 5 – D5
 *   Bit 6 – D6
 *   Bit 7 – D7
 *
 * 4-bit mode: each byte sent as two nibbles (high nibble first).
 * Backlight is always on.
 *
 * cfg->i2c_address : PCF8574 address (default 0x27)
 * cfg->bus_index   : I2C port number
 * cfg->display_cols: columns (default 16)
 * cfg->display_rows: rows    (default 2)
 *
 * Write CAP_TEXT_DISPLAY: cmd->s null-terminated string.
 *   Supports \n to advance to next row.
 *   Rows beyond display_rows are silently discarded.
 *
 * Max 2 LCD instances.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"
#include <string.h>

static const char *TAG = "DRV_LCD_I2C";

#define LCD_MAX_INSTANCES       2
#define LCD_DEFAULT_ADDR        0x27
#define LCD_DEFAULT_COLS        16
#define LCD_DEFAULT_ROWS        2
#define LCD_MAX_COLS            40
#define LCD_MAX_ROWS            4

/* PCF8574 bit masks */
#define LCD_RS   0x01
#define LCD_RW   0x02
#define LCD_EN   0x04
#define LCD_BL   0x08
#define LCD_D4   0x10
#define LCD_D5   0x20
#define LCD_D6   0x40
#define LCD_D7   0x80

/* HD44780 row start addresses */
static const uint8_t s_row_offsets[4] = {0x00, 0x40, 0x14, 0x54};

/* HD44780 commands */
#define LCD_CMD_CLEAR           0x01
#define LCD_CMD_RETURN_HOME     0x02
#define LCD_CMD_ENTRY_MODE      0x06   /* increment, no shift */
#define LCD_CMD_DISPLAY_ON      0x0C   /* display on, cursor off, blink off */
#define LCD_CMD_FUNCTION_4BIT   0x28   /* 4-bit, 2 lines, 5×8 dots */
#define LCD_CMD_SET_DDRAM       0x80

extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

/* Write a raw byte directly to PCF8574 (no sub-register) */
static esp_err_t pcf8574_write_raw(uint8_t port, uint8_t addr, uint8_t byte)
{
    /* i2c_bus_write uses a register byte before data; for PCF8574 there is no
       register — the single byte IS the data. We repurpose reg=byte, data=NULL.
       A minimal workaround: send the byte as the register with zero data length.
       However i2c_bus_write always writes reg + data. We send the payload byte
       as a 1-byte "data" to reg=0 (ignored by PCF8574 which has no reg map). */
    return i2c_bus_write(port, addr, byte, NULL, 0);
}

static esp_err_t lcd_pulse_enable(uint8_t port, uint8_t addr, uint8_t data_byte)
{
    /* data_byte already contains RS/BL bits; D7..D4 nibble */
    esp_err_t err;
    err = pcf8574_write_raw(port, addr, data_byte | LCD_EN);
    esp_rom_delay_us(1);
    err |= pcf8574_write_raw(port, addr, data_byte & ~LCD_EN);
    esp_rom_delay_us(50);
    return err;
}

static esp_err_t lcd_send_nibble(uint8_t port, uint8_t addr, uint8_t nibble, uint8_t flags)
{
    /* nibble is upper 4 bits; shift into D7..D4 positions */
    uint8_t data = (nibble & 0xF0) | flags | LCD_BL;
    return lcd_pulse_enable(port, addr, data);
}

static esp_err_t lcd_send_byte(uint8_t port, uint8_t addr, uint8_t byte, uint8_t flags)
{
    esp_err_t err;
    err  = lcd_send_nibble(port, addr,  byte & 0xF0,        flags);  /* high nibble */
    err |= lcd_send_nibble(port, addr, (byte << 4) & 0xF0,  flags);  /* low nibble  */
    return err;
}

static inline esp_err_t lcd_send_cmd(uint8_t port, uint8_t addr, uint8_t cmd)
{
    return lcd_send_byte(port, addr, cmd, 0);  /* RS=0 */
}

static inline esp_err_t lcd_send_data(uint8_t port, uint8_t addr, uint8_t data)
{
    return lcd_send_byte(port, addr, data, LCD_RS); /* RS=1 */
}

/* ---- HD44780 4-bit initialisation sequence (as per datasheet page 46) ---- */
static esp_err_t lcd_hw_init(uint8_t port, uint8_t addr)
{
    /* Wait for LCD power-on reset (>40 ms) */
    esp_rom_delay_us(50000);

    /* Three function-set nibbles to reliably enter 4-bit mode */
    lcd_send_nibble(port, addr, 0x30, 0); esp_rom_delay_us(4500);
    lcd_send_nibble(port, addr, 0x30, 0); esp_rom_delay_us(150);
    lcd_send_nibble(port, addr, 0x30, 0); esp_rom_delay_us(150);
    lcd_send_nibble(port, addr, 0x20, 0); /* switch to 4-bit mode */
    esp_rom_delay_us(150);

    /* From here on, full 2-nibble bytes work */
    lcd_send_cmd(port, addr, LCD_CMD_FUNCTION_4BIT); esp_rom_delay_us(150);
    lcd_send_cmd(port, addr, LCD_CMD_DISPLAY_ON);    esp_rom_delay_us(150);
    lcd_send_cmd(port, addr, LCD_CMD_CLEAR);          esp_rom_delay_us(2000);
    lcd_send_cmd(port, addr, LCD_CMD_ENTRY_MODE);     esp_rom_delay_us(150);
    return ESP_OK;
}

/* ---------- instance state ------------------------------------------------- */

typedef struct {
    uint8_t i2c_port;
    uint8_t address;
    uint8_t cols;
    uint8_t rows;
    char    framebuffer[LCD_MAX_ROWS][LCD_MAX_COLS + 1];
    bool    initialized;
} lcd_state_t;

static lcd_state_t s_state[LCD_MAX_INSTANCES];
static uint8_t s_count = 0;

/* ---------- ABI ------------------------------------------------------------ */

static esp_err_t lcd_i2c_init(const driver_config_t *cfg)
{
    if (s_count >= LCD_MAX_INSTANCES) {
        ESP_LOGE(TAG, "Max LCD instances (%d) reached", LCD_MAX_INSTANCES);
        return ESP_ERR_NO_MEM;
    }

    lcd_state_t *st = &s_state[s_count];
    st->i2c_port = cfg->bus_index;
    st->address  = cfg->i2c_address ? cfg->i2c_address : LCD_DEFAULT_ADDR;
    st->cols     = cfg->display_cols ? cfg->display_cols : LCD_DEFAULT_COLS;
    st->rows     = cfg->display_rows ? cfg->display_rows : LCD_DEFAULT_ROWS;
    if (st->cols > LCD_MAX_COLS) st->cols = LCD_MAX_COLS;
    if (st->rows > LCD_MAX_ROWS) st->rows = LCD_MAX_ROWS;
    memset(st->framebuffer, ' ', sizeof(st->framebuffer));

    esp_err_t err = lcd_hw_init(st->i2c_port, st->address);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LCD hardware init failed: %d", err);
        return err;
    }

    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "LCD[%d] at 0x%02X on I2C%d, %dx%d",
             s_count - 1, st->address, st->i2c_port, st->cols, st->rows);
    return ESP_OK;
}

static esp_err_t lcd_i2c_write(driver_handle_t h, const actuator_cmd_t *cmd)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    lcd_state_t *st = &s_state[idx];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    if (cmd->capability != CAP_TEXT_DISPLAY) return ESP_ERR_NOT_SUPPORTED;
    if (cmd->type != VAL_TYPE_STRING) return ESP_ERR_INVALID_ARG;

    /* Parse string into framebuffer rows */
    const char *src = cmd->s;
    uint8_t row = 0, col = 0;

    /* Clear framebuffer */
    for (int r = 0; r < st->rows; r++) {
        memset(st->framebuffer[r], ' ', st->cols);
        st->framebuffer[r][st->cols] = '\0';
    }

    while (*src && row < st->rows) {
        if (*src == '\n') {
            row++;
            col = 0;
        } else if (col < st->cols) {
            st->framebuffer[row][col++] = *src;
        }
        src++;
    }

    /* Flush framebuffer to LCD */
    lcd_send_cmd(st->i2c_port, st->address, LCD_CMD_CLEAR);
    esp_rom_delay_us(2000);

    for (int r = 0; r < st->rows; r++) {
        uint8_t ddram = LCD_CMD_SET_DDRAM | s_row_offsets[r];
        lcd_send_cmd(st->i2c_port, st->address, ddram);
        esp_rom_delay_us(50);
        for (int c = 0; c < st->cols; c++) {
            lcd_send_data(st->i2c_port, st->address, (uint8_t)st->framebuffer[r][c]);
        }
    }

    ESP_LOGD(TAG, "LCD[%d] wrote: \"%.*s\"", idx, DRIVER_MAX_STRING_VAL, cmd->s);
    return ESP_OK;
}

static esp_err_t lcd_i2c_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    (void)h; (void)field;
    out->error = DRV_ERR_NOT_SUPPORTED;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t lcd_i2c_deinit(driver_handle_t h)
{
    uint8_t idx = h.driver_index;
    if (idx < s_count && s_state[idx].initialized) {
        lcd_state_t *st = &s_state[idx];
        lcd_send_cmd(st->i2c_port, st->address, LCD_CMD_CLEAR);
        s_state[idx].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t lcd_i2c_meta = {
    .name             = "drv_lcd_i2c",
    .display_name     = "HD44780 LCD (PCF8574 I2C)",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_DISPLAY,
    .bus_type         = BUS_TYPE_I2C,
    .capabilities     = {CAP_TEXT_DISPLAY},
    .num_capabilities = 1,
    .max_latency_us   = 5000,
    .min_interval_ms  = 50,
    .failure_modes    = {
        {DRV_ERR_BUS_FAIL, "I2C communication failure", {.type = VAL_TYPE_STRING}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(lcd_state_t),
};

const driver_vtable_t drv_lcd_i2c_vtable = {
    .init   = lcd_i2c_init,
    .read   = lcd_i2c_read,
    .write  = lcd_i2c_write,
    .deinit = lcd_i2c_deinit,
    .meta   = &lcd_i2c_meta,
};
