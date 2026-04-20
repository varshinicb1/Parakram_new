/**
 * @file ble_gatt_profile.c
 * @brief NimBLE GATT server for Parakram — device info, deployment, and telemetry.
 */

#include "comms.h"
#include "event_bus.h"
#include "system_config.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

static const char *TAG = "BLE";

static bool s_ble_initialized = false;
static bool s_connected = false;
static uint16_t s_conn_handle = 0;
static ble_payload_rx_cb_t s_payload_cb = NULL;

/* Static RX buffer for chunked payload assembly */
static uint8_t s_rx_buffer[SYS_PROGRAM_MAX_SIZE];
static uint32_t s_rx_offset = 0;
static uint32_t s_rx_expected = 0;

/* Parakram service UUIDs */
static const ble_uuid128_t svc_deploy_uuid = BLE_UUID128_INIT(
    0x01, 0x00, 0x00, 0x00, 0xAB, 0xCD, 0xEF, 0x12,
    0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0x12);

static const ble_uuid128_t chr_payload_uuid = BLE_UUID128_INIT(
    0x02, 0x00, 0x00, 0x00, 0xAB, 0xCD, 0xEF, 0x12,
    0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0x12);

static const ble_uuid128_t chr_status_uuid = BLE_UUID128_INIT(
    0x03, 0x00, 0x00, 0x00, 0xAB, 0xCD, 0xEF, 0x12,
    0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0x12);

static uint16_t s_status_handle = 0;

/* GATT access callback for deployment service */
static int deploy_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        /* Receive chunk */
        struct os_mbuf *om = ctxt->om;
        uint16_t len = OS_MBUF_PKTLEN(om);

        if (s_rx_offset == 0 && len >= 4) {
            /* First chunk contains length header */
            uint8_t hdr[4];
            os_mbuf_copydata(om, 0, 4, hdr);
            s_rx_expected = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) |
                           ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);

            if (s_rx_expected > SYS_PROGRAM_MAX_SIZE) {
                ESP_LOGE(TAG, "Payload too large: %lu", (unsigned long)s_rx_expected);
                s_rx_offset = 0;
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }

            uint16_t data_len = len - 4;
            os_mbuf_copydata(om, 4, data_len, s_rx_buffer);
            s_rx_offset = data_len;
        } else {
            /* Subsequent chunks */
            if (s_rx_offset + len <= SYS_PROGRAM_MAX_SIZE) {
                os_mbuf_copydata(om, 0, len, s_rx_buffer + s_rx_offset);
                s_rx_offset += len;
            }
        }

        /* Check if transfer complete */
        if (s_rx_offset >= s_rx_expected && s_rx_expected > 0) {
            ESP_LOGI(TAG, "BLE payload complete: %lu bytes", (unsigned long)s_rx_offset);
            if (s_payload_cb) {
                s_payload_cb(s_rx_buffer, s_rx_offset);
            }
            s_rx_offset = 0;
            s_rx_expected = 0;
        }

        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int status_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const char *status = "ready";
        os_mbuf_append(ctxt->om, status, strlen(status));
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/* GATT service table */
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_deploy_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &chr_payload_uuid.u,
                .access_cb = deploy_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &chr_status_uuid.u,
                .access_cb = status_access_cb,
                .val_handle = &s_status_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }, /* sentinel */
        },
    },
    { 0 }, /* sentinel */
};

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_connected = true;
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "BLE connected, handle=%d", s_conn_handle);
            event_t evt = {.type = EVT_BLE_CONNECTED};
            event_bus_publish(&evt);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_connected = false;
        ESP_LOGI(TAG, "BLE disconnected");
        event_t evt = {.type = EVT_BLE_DISCONNECTED};
        event_bus_publish(&evt);
        ble_gatt_start_advertising(); /* Re-advertise */
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated to %d", event->mtu.value);
        break;
    }
    return 0;
}

static void ble_host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_gatt_init(const char *device_name) {
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BLE controller init failed (err=%d), running WiFi-only mode", ret);
        s_ble_initialized = false;
        return ESP_FAIL;
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    ble_svc_gap_device_name_set(device_name);

    nimble_port_freertos_init(ble_host_task);

    s_ble_initialized = true;
    ESP_LOGI(TAG, "BLE GATT initialized: %s", device_name);
    return ESP_OK;
}

esp_err_t ble_gatt_start_advertising(void) {
    if (!s_ble_initialized) return ESP_ERR_INVALID_STATE;
    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    ble_gap_adv_set_fields(&fields);
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                      &adv_params, ble_gap_event_cb, NULL);

    ESP_LOGI(TAG, "BLE advertising started");
    return ESP_OK;
}

esp_err_t ble_gatt_notify(uint16_t char_handle, const uint8_t *data, uint16_t len) {
    if (!s_ble_initialized || !s_connected) return ESP_ERR_INVALID_STATE;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    return ble_gatts_notify_custom(s_conn_handle, char_handle, om);
}

bool ble_gatt_is_connected(void) { return s_connected; }
void ble_gatt_set_payload_callback(ble_payload_rx_cb_t cb) { s_payload_cb = cb; }
