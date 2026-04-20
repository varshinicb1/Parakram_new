/**
 * @file drv_oled_ssd1306.c
 * @brief SSD1306 128×64 OLED display driver over I2C.
 *
 * I2C address: 0x3C (default) or 0x3D.
 * Co byte 0x00 = command stream, 0x40 = data stream.
 *
 * Framebuffer layout: 128 columns × 8 pages (rows/8), 1 bit per pixel.
 * Total: 1024 bytes static per instance.
 *
 * Font: built-in 5×7 ASCII glyphs (printable chars 0x20–0x7E).
 * Each glyph is 5 bytes wide (columns), 1 byte = 8 pixel rows.
 * Rendered left-to-right, top-to-bottom on 8-pixel page boundaries.
 *
 * Write CAP_TEXT_DISPLAY (string): clears framebuffer, renders text,
 * then flushes entire framebuffer to OLED via I2C.
 *
 * Max 2 OLED instances.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "DRV_OLED";

#define OLED_MAX_INSTANCES      2
#define OLED_DEFAULT_ADDR       0x3C
#define OLED_WIDTH              128
#define OLED_HEIGHT             64
#define OLED_PAGES              (OLED_HEIGHT / 8)   /* 8 pages */
#define OLED_FB_SIZE            (OLED_WIDTH * OLED_PAGES)   /* 1024 bytes */

#define OLED_CTRL_CMD           0x00
#define OLED_CTRL_DATA          0x40

extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

/* ---- 5×7 ASCII font (chars 0x20 – 0x7E, 5 bytes each column pattern) ---- */
/* Each byte is a column of 8 pixels (bit 0 = top). Sourced from classic font. */
static const uint8_t s_font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* 0x20 ' ' */
    {0x00,0x00,0x5F,0x00,0x00}, /* 0x21 '!' */
    {0x00,0x07,0x00,0x07,0x00}, /* 0x22 '"' */
    {0x14,0x7F,0x14,0x7F,0x14}, /* 0x23 '#' */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* 0x24 '$' */
    {0x23,0x13,0x08,0x64,0x62}, /* 0x25 '%' */
    {0x36,0x49,0x55,0x22,0x50}, /* 0x26 '&' */
    {0x00,0x05,0x03,0x00,0x00}, /* 0x27 ''' */
    {0x00,0x1C,0x22,0x41,0x00}, /* 0x28 '(' */
    {0x00,0x41,0x22,0x1C,0x00}, /* 0x29 ')' */
    {0x08,0x2A,0x1C,0x2A,0x08}, /* 0x2A '*' */
    {0x08,0x08,0x3E,0x08,0x08}, /* 0x2B '+' */
    {0x00,0x50,0x30,0x00,0x00}, /* 0x2C ',' */
    {0x08,0x08,0x08,0x08,0x08}, /* 0x2D '-' */
    {0x00,0x60,0x60,0x00,0x00}, /* 0x2E '.' */
    {0x20,0x10,0x08,0x04,0x02}, /* 0x2F '/' */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0x30 '0' */
    {0x00,0x42,0x7F,0x40,0x00}, /* 0x31 '1' */
    {0x42,0x61,0x51,0x49,0x46}, /* 0x32 '2' */
    {0x21,0x41,0x45,0x4B,0x31}, /* 0x33 '3' */
    {0x18,0x14,0x12,0x7F,0x10}, /* 0x34 '4' */
    {0x27,0x45,0x45,0x45,0x39}, /* 0x35 '5' */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 0x36 '6' */
    {0x01,0x71,0x09,0x05,0x03}, /* 0x37 '7' */
    {0x36,0x49,0x49,0x49,0x36}, /* 0x38 '8' */
    {0x06,0x49,0x49,0x29,0x1E}, /* 0x39 '9' */
    {0x00,0x36,0x36,0x00,0x00}, /* 0x3A ':' */
    {0x00,0x56,0x36,0x00,0x00}, /* 0x3B ';' */
    {0x08,0x14,0x22,0x41,0x00}, /* 0x3C '<' */
    {0x14,0x14,0x14,0x14,0x14}, /* 0x3D '=' */
    {0x00,0x41,0x22,0x14,0x08}, /* 0x3E '>' */
    {0x02,0x01,0x51,0x09,0x06}, /* 0x3F '?' */
    {0x32,0x49,0x79,0x41,0x3E}, /* 0x40 '@' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 0x41 'A' */
    {0x7F,0x49,0x49,0x49,0x36}, /* 0x42 'B' */
    {0x3E,0x41,0x41,0x41,0x22}, /* 0x43 'C' */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 0x44 'D' */
    {0x7F,0x49,0x49,0x49,0x41}, /* 0x45 'E' */
    {0x7F,0x09,0x09,0x09,0x01}, /* 0x46 'F' */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 0x47 'G' */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 0x48 'H' */
    {0x00,0x41,0x7F,0x41,0x00}, /* 0x49 'I' */
    {0x20,0x40,0x41,0x3F,0x01}, /* 0x4A 'J' */
    {0x7F,0x08,0x14,0x22,0x41}, /* 0x4B 'K' */
    {0x7F,0x40,0x40,0x40,0x40}, /* 0x4C 'L' */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* 0x4D 'M' */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 0x4E 'N' */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 0x4F 'O' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 0x50 'P' */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 0x51 'Q' */
    {0x7F,0x09,0x19,0x29,0x46}, /* 0x52 'R' */
    {0x46,0x49,0x49,0x49,0x31}, /* 0x53 'S' */
    {0x01,0x01,0x7F,0x01,0x01}, /* 0x54 'T' */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 0x55 'U' */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 0x56 'V' */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 0x57 'W' */
    {0x63,0x14,0x08,0x14,0x63}, /* 0x58 'X' */
    {0x07,0x08,0x70,0x08,0x07}, /* 0x59 'Y' */
    {0x61,0x51,0x49,0x45,0x43}, /* 0x5A 'Z' */
    {0x00,0x7F,0x41,0x41,0x00}, /* 0x5B '[' */
    {0x02,0x04,0x08,0x10,0x20}, /* 0x5C '\' */
    {0x00,0x41,0x41,0x7F,0x00}, /* 0x5D ']' */
    {0x04,0x02,0x01,0x02,0x04}, /* 0x5E '^' */
    {0x40,0x40,0x40,0x40,0x40}, /* 0x5F '_' */
    {0x00,0x01,0x02,0x04,0x00}, /* 0x60 '`' */
    {0x20,0x54,0x54,0x54,0x78}, /* 0x61 'a' */
    {0x7F,0x48,0x44,0x44,0x38}, /* 0x62 'b' */
    {0x38,0x44,0x44,0x44,0x20}, /* 0x63 'c' */
    {0x38,0x44,0x44,0x48,0x7F}, /* 0x64 'd' */
    {0x38,0x54,0x54,0x54,0x18}, /* 0x65 'e' */
    {0x08,0x7E,0x09,0x01,0x02}, /* 0x66 'f' */
    {0x0C,0x52,0x52,0x52,0x3E}, /* 0x67 'g' */
    {0x7F,0x08,0x04,0x04,0x78}, /* 0x68 'h' */
    {0x00,0x44,0x7D,0x40,0x00}, /* 0x69 'i' */
    {0x20,0x40,0x44,0x3D,0x00}, /* 0x6A 'j' */
    {0x7F,0x10,0x28,0x44,0x00}, /* 0x6B 'k' */
    {0x00,0x41,0x7F,0x40,0x00}, /* 0x6C 'l' */
    {0x7C,0x04,0x18,0x04,0x78}, /* 0x6D 'm' */
    {0x7C,0x08,0x04,0x04,0x78}, /* 0x6E 'n' */
    {0x38,0x44,0x44,0x44,0x38}, /* 0x6F 'o' */
    {0x7C,0x14,0x14,0x14,0x08}, /* 0x70 'p' */
    {0x08,0x14,0x14,0x18,0x7C}, /* 0x71 'q' */
    {0x7C,0x08,0x04,0x04,0x08}, /* 0x72 'r' */
    {0x48,0x54,0x54,0x54,0x20}, /* 0x73 's' */
    {0x04,0x3F,0x44,0x40,0x20}, /* 0x74 't' */
    {0x3C,0x40,0x40,0x20,0x7C}, /* 0x75 'u' */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 0x76 'v' */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 0x77 'w' */
    {0x44,0x28,0x10,0x28,0x44}, /* 0x78 'x' */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 0x79 'y' */
    {0x44,0x64,0x54,0x4C,0x44}, /* 0x7A 'z' */
    {0x00,0x08,0x36,0x41,0x00}, /* 0x7B '{' */
    {0x00,0x00,0x7F,0x00,0x00}, /* 0x7C '|' */
    {0x00,0x41,0x36,0x08,0x00}, /* 0x7D '}' */
    {0x10,0x08,0x08,0x10,0x08}, /* 0x7E '~' */
};

/* ---- SSD1306 initialisation commands ---------------------------------------- */
static const uint8_t s_init_cmds[] = {
    0xAE,        /* display off */
    0xD5, 0x80,  /* clock div / osc freq */
    0xA8, 0x3F,  /* multiplex ratio (64-1) */
    0xD3, 0x00,  /* display offset */
    0x40,        /* start line 0 */
    0x8D, 0x14,  /* charge pump enable */
    0x20, 0x00,  /* memory mode: horizontal */
    0xA1,        /* seg remap (col 127→SEG0) */
    0xC8,        /* com scan dec */
    0xDA, 0x12,  /* com pins config */
    0x81, 0xCF,  /* contrast */
    0xD9, 0xF1,  /* pre-charge */
    0xDB, 0x40,  /* vcomh deselect level */
    0xA4,        /* display all on resume */
    0xA6,        /* normal display (not inverted) */
    0xAF,        /* display on */
};

/* ---- instance state --------------------------------------------------------- */

typedef struct {
    uint8_t i2c_port;
    uint8_t address;
    uint8_t framebuffer[OLED_FB_SIZE]; /* 128 × 8 pages */
    bool    initialized;
} oled_state_t;

static oled_state_t s_state[OLED_MAX_INSTANCES];
static uint8_t s_count = 0;

/* Send a command byte */
static esp_err_t oled_cmd(uint8_t port, uint8_t addr, uint8_t cmd)
{
    return i2c_bus_write(port, addr, OLED_CTRL_CMD, &cmd, 1);
}

/* Flush full framebuffer */
static esp_err_t oled_flush(oled_state_t *st)
{
    /* Set column and page address to cover entire display */
    oled_cmd(st->i2c_port, st->address, 0x21);  /* set column address */
    oled_cmd(st->i2c_port, st->address, 0x00);
    oled_cmd(st->i2c_port, st->address, 0x7F);
    oled_cmd(st->i2c_port, st->address, 0x22);  /* set page address */
    oled_cmd(st->i2c_port, st->address, 0x00);
    oled_cmd(st->i2c_port, st->address, 0x07);

    /* Send framebuffer in chunks (I2C buffer limit) */
    const uint16_t CHUNK = 32;
    for (uint16_t offset = 0; offset < OLED_FB_SIZE; offset += CHUNK) {
        uint16_t len = (offset + CHUNK <= OLED_FB_SIZE) ? CHUNK : (OLED_FB_SIZE - offset);
        esp_err_t err = i2c_bus_write(st->i2c_port, st->address,
                                      OLED_CTRL_DATA,
                                      st->framebuffer + offset, len);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

/* Draw a single character at (col_px, page) — col_px in [0,127], page in [0,7] */
static void oled_draw_char(oled_state_t *st, uint8_t page, uint8_t col_px, char c)
{
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t *glyph = s_font5x7[(uint8_t)(c - 0x20)];
    for (int i = 0; i < 5; i++) {
        uint16_t fb_idx = (uint16_t)page * OLED_WIDTH + col_px + i;
        if (fb_idx < OLED_FB_SIZE) {
            st->framebuffer[fb_idx] = glyph[i];
        }
    }
    /* 1-pixel gap after glyph */
    uint16_t gap_idx = (uint16_t)page * OLED_WIDTH + col_px + 5;
    if (gap_idx < OLED_FB_SIZE) {
        st->framebuffer[gap_idx] = 0x00;
    }
}

/* Characters per row: floor(128 / 6) = 21 */
#define OLED_CHARS_PER_ROW  21
/* Text rows (pages per text row = 1, so 8 text lines on 64px display) */
#define OLED_TEXT_ROWS      8

/* ---------- ABI -------------------------------------------------------------- */

static esp_err_t oled_ssd1306_init(const driver_config_t *cfg)
{
    if (s_count >= OLED_MAX_INSTANCES) {
        ESP_LOGE(TAG, "Max OLED instances (%d) reached", OLED_MAX_INSTANCES);
        return ESP_ERR_NO_MEM;
    }

    oled_state_t *st = &s_state[s_count];
    st->i2c_port = cfg->bus_index;
    st->address  = cfg->i2c_address ? cfg->i2c_address : OLED_DEFAULT_ADDR;
    memset(st->framebuffer, 0, sizeof(st->framebuffer));

    /* Send init sequence */
    for (size_t i = 0; i < sizeof(s_init_cmds); i++) {
        esp_err_t err = oled_cmd(st->i2c_port, st->address, s_init_cmds[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Init cmd 0x%02X failed: %d", s_init_cmds[i], err);
            return err;
        }
    }

    /* Clear display */
    oled_flush(st);

    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "SSD1306 OLED[%d] at 0x%02X on I2C%d (128×64)",
             s_count - 1, st->address, st->i2c_port);
    return ESP_OK;
}

static esp_err_t oled_ssd1306_write(driver_handle_t h, const actuator_cmd_t *cmd)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    oled_state_t *st = &s_state[idx];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    if (cmd->capability != CAP_TEXT_DISPLAY) return ESP_ERR_NOT_SUPPORTED;
    if (cmd->type != VAL_TYPE_STRING)        return ESP_ERR_INVALID_ARG;

    /* Clear framebuffer */
    memset(st->framebuffer, 0, sizeof(st->framebuffer));

    const char *src = cmd->s;
    uint8_t page = 0;
    uint8_t col  = 0;

    while (*src && page < OLED_TEXT_ROWS) {
        char c = *src++;
        if (c == '\n') {
            page++;
            col = 0;
            continue;
        }
        if (col >= OLED_CHARS_PER_ROW) {
            page++;
            col = 0;
            if (page >= OLED_TEXT_ROWS) break;
        }
        oled_draw_char(st, page, (uint8_t)(col * 6), c);
        col++;
    }

    ESP_LOGD(TAG, "OLED[%d] rendering: \"%.*s\"", idx, DRIVER_MAX_STRING_VAL, cmd->s);
    return oled_flush(st);
}

static esp_err_t oled_ssd1306_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    (void)h; (void)field;
    out->error = DRV_ERR_NOT_SUPPORTED;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t oled_ssd1306_deinit(driver_handle_t h)
{
    uint8_t idx = h.driver_index;
    if (idx < s_count && s_state[idx].initialized) {
        oled_state_t *st = &s_state[idx];
        memset(st->framebuffer, 0, sizeof(st->framebuffer));
        oled_flush(st);
        oled_cmd(st->i2c_port, st->address, 0xAE); /* display off */
        st->initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t oled_ssd1306_meta = {
    .name             = "drv_oled_ssd1306",
    .display_name     = "SSD1306 128x64 OLED",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_DISPLAY,
    .bus_type         = BUS_TYPE_I2C,
    .capabilities     = {CAP_TEXT_DISPLAY},
    .num_capabilities = 1,
    .max_latency_us   = 20000,
    .min_interval_ms  = 33,
    .failure_modes    = {
        {DRV_ERR_BUS_FAIL, "I2C communication failure", {.type = VAL_TYPE_STRING}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(oled_state_t),
};

const driver_vtable_t drv_oled_ssd1306_vtable = {
    .init   = oled_ssd1306_init,
    .read   = oled_ssd1306_read,
    .write  = oled_ssd1306_write,
    .deinit = oled_ssd1306_deinit,
    .meta   = &oled_ssd1306_meta,
};
