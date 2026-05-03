#include "ble_service.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "BLE";

// Nordic UART Service UUIDs
#define NUS_SERVICE_UUID       "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_CHAR_UUID       "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_TX_CHAR_UUID       "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// GATT attribute table indices (sequential)
#define GATTS_ATTR_SERVICE_DECL  0  // Service declaration
#define GATTS_ATTR_RX_DECL       1  // RX characteristic declaration
#define GATTS_ATTR_RX_VAL        2  // RX characteristic value
#define GATTS_ATTR_TX_DECL       3  // TX characteristic declaration
#define GATTS_ATTR_TX_VAL        4  // TX characteristic value
#define GATTS_ATTR_TX_CCCD       5  // TX client characteristic configuration descriptor
#define GATTS_NUM_HANDLES        6

#define LINE_BUF_SIZE           512
#define DEVICE_NAME_PREFIX      "Claude-Buddy-"

// 128-bit UUID storage
static const uint8_t nus_service_uuid[16] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e
};
static const uint8_t nus_rx_char_uuid[16] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e
};
static const uint8_t nus_tx_char_uuid[16] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e
};

static char device_name[32] = DEVICE_NAME_PREFIX;

// State
static bool is_connected = false;
static uint16_t conn_id = 0;
static esp_gatt_if_t gatts_if = ESP_GATT_IF_NONE;
static uint16_t rx_handle = 0;
static uint16_t tx_handle = 0;
static uint16_t tx_ccc_handle = 0;
static bool tx_notify_enabled = false;

static uint16_t app_id = 0x55;
static bool service_created = false;

// Line buffer for incoming data
static char line_buf[LINE_BUF_SIZE];
static int line_len = 0;

// Callbacks
static ble_callbacks_t callbacks = {0};

// Forward declarations
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t if_val,
                                esp_ble_gatts_cb_param_t *param);
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void parse_json_line(const char *line, int len);
static void start_advertising(void);
static void send_tx_data(const char *data, int len);

// ---- Helper: invoke callbacks ----

static void invoke_on_connected(void)
{
    if (callbacks.on_connected) {
        callbacks.on_connected();
    }
}

static void invoke_on_disconnected(void)
{
    if (callbacks.on_disconnected) {
        callbacks.on_disconnected();
    }
}

static void invoke_on_heartbeat(const ble_heartbeat_t *hb)
{
    if (callbacks.on_heartbeat) {
        callbacks.on_heartbeat(hb);
    }
}

// ---- GAP event handler ----

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            ESP_LOGD(TAG, "Adv data set complete, status=%d", param->adv_data_cmpl.status);
            break;

        case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
            ESP_LOGD(TAG, "Scan resp data set complete");
            break;

        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(TAG, "Adv start failed: %d", param->adv_start_cmpl.status);
            } else {
                ESP_LOGI(TAG, "Advertising started");
            }
            break;

        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(TAG, "Adv stop failed: %d", param->adv_stop_cmpl.status);
            }
            break;

        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            if (param->update_conn_params.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGD(TAG, "Update conn params failed: %d", param->update_conn_params.status);
            }
            break;

        default:
            break;
    }
}

// ---- Advertising ----

static void start_advertising(void)
{
    // Advertising data: flags + 128-bit service UUID (incomplete list)
    uint8_t adv_data[20];
    int pos = 0;

    // Flags
    adv_data[pos++] = 0x02;  // length
    adv_data[pos++] = ESP_BLE_AD_TYPE_FLAG;
    adv_data[pos++] = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT;

    // Incomplete list of 128-bit service UUIDs
    adv_data[pos++] = 17;  // length: 1 (type) + 16 (UUID)
    adv_data[pos++] = ESP_BLE_AD_TYPE_128SRV_PART;
    memcpy(&adv_data[pos], nus_service_uuid, 16);
    pos += 16;

    esp_ble_gap_config_adv_data_raw(adv_data, pos);

    // Scan response: device name
    uint8_t scan_rsp[32];
    int name_len = strlen(device_name);
    int rsp_pos = 0;

    scan_rsp[rsp_pos++] = 1 + name_len;
    scan_rsp[rsp_pos++] = ESP_BLE_AD_TYPE_NAME_CMPL;
    memcpy(&scan_rsp[rsp_pos], device_name, name_len);
    rsp_pos += name_len;

    esp_ble_gap_config_scan_rsp_data_raw(scan_rsp, rsp_pos);

    // Start advertising
    esp_ble_adv_params_t adv_params = {
        .adv_int_min = 0x0020,  // 20ms
        .adv_int_max = 0x0040,  // 40ms
        .adv_type = ADV_TYPE_IND,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .channel_map = ADV_CHNL_ALL,
        .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    };
    esp_ble_gap_start_advertising(&adv_params);
}

// ---- TX send ----

static void send_tx_data(const char *data, int len)
{
    if (!is_connected || !tx_notify_enabled) {
        ESP_LOGD(TAG, "TX not ready (connected=%d, notify=%d)", is_connected, tx_notify_enabled);
        return;
    }
    esp_ble_gatts_send_indicate(gatts_if, conn_id, tx_handle, len, (uint8_t *)data, false);
}

// ---- JSON parsing ----

static void json_copy_str(cJSON *obj, const char *key, char *dst, int dst_size)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsString(item)) {
        strncpy(dst, item->valuestring, dst_size - 1);
        dst[dst_size - 1] = '\0';
    }
}

static void json_copy_int(cJSON *obj, const char *key, int *dst)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsNumber(item)) {
        *dst = item->valueint;
    }
}

static void parse_json_line(const char *line, int len)
{
    // Null-terminate for cJSON
    char *json_str = malloc(len + 1);
    if (!json_str) {
        ESP_LOGE(TAG, "Failed to allocate JSON buffer");
        return;
    }
    memcpy(json_str, line, len);
    json_str[len] = '\0';

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGD(TAG, "Invalid JSON: %s", json_str);
        free(json_str);
        return;
    }

    // Check for command messages
    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    if (cmd && cJSON_IsString(cmd)) {
        const char *cmd_str = cmd->valuestring;
        ESP_LOGI(TAG, "Received cmd: %s", cmd_str);

        if (strcmp(cmd_str, "status") == 0) {
            ble_service_send_ack("status", true);
        } else if (strcmp(cmd_str, "name") == 0) {
            // Buddy name received (acknowledge)
            ble_service_send_ack("name", true);
        } else if (strcmp(cmd_str, "owner") == 0) {
            // Owner name received (acknowledge)
            ble_service_send_ack("owner", true);
        } else if (strcmp(cmd_str, "unpair") == 0) {
            ESP_LOGI(TAG, "Unpair requested, clearing bonds");
            // Remove all bonded devices
            int dev_num = esp_ble_get_bond_device_num();
            if (dev_num > 0) {
                esp_ble_bond_dev_t *dev_list = malloc(sizeof(esp_ble_bond_dev_t) * dev_num);
                if (dev_list) {
                    esp_ble_get_bond_device_list(&dev_num, dev_list);
                    for (int i = 0; i < dev_num; i++) {
                        esp_ble_remove_bond_device(dev_list[i].bd_addr);
                    }
                    free(dev_list);
                }
            }
            ble_service_send_ack("unpair", true);
        }
        cJSON_Delete(root);
        free(json_str);
        return;
    }

    // Parse heartbeat data
    ble_heartbeat_t hb = {0};
    json_copy_int(root, "total", &hb.total);
    json_copy_int(root, "running", &hb.running);
    json_copy_int(root, "waiting", &hb.waiting);
    json_copy_str(root, "msg", hb.msg, sizeof(hb.msg));

    // Check for prompt object
    cJSON *prompt = cJSON_GetObjectItem(root, "prompt");
    if (prompt && cJSON_IsObject(prompt)) {
        hb.has_prompt = true;
        json_copy_str(prompt, "id", hb.prompt_id, sizeof(hb.prompt_id));
        json_copy_str(prompt, "tool", hb.prompt_tool, sizeof(hb.prompt_tool));
        json_copy_str(prompt, "hint", hb.prompt_hint, sizeof(hb.prompt_hint));
    }

    invoke_on_heartbeat(&hb);

    cJSON_Delete(root);
    free(json_str);
}

// ---- GATTS event handler ----

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t if_val,
                                esp_ble_gatts_cb_param_t *param)
{
    // Handle registration event for our app
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gatts_if = if_val;
            ESP_LOGI(TAG, "GATT app registered, if=%d", if_val);
        } else {
            ESP_LOGE(TAG, "GATT app register failed: %d", param->reg.status);
            return;
        }
    }

    // Only process events for our interface
    if (if_val != gatts_if && event != ESP_GATTS_REG_EVT) {
        return;
    }

    switch (event) {
        case ESP_GATTS_REG_EVT: {
            // Set device name
            esp_ble_gap_set_device_name(device_name);

            // Create attribute table
            esp_ble_gatts_create_attr_tab(
                (esp_gatts_attr_db_t[]) {
                    // Service declaration
                    [GATTS_ATTR_SERVICE_DECL] = {
                        {ESP_GATT_AUTO_RSP},
                        {ESP_UUID_LEN_16, (uint8_t *)&(uint16_t){ESP_GATT_UUID_PRI_SERVICE},
                         ESP_GATT_PERM_READ, sizeof(uint16_t), sizeof(nus_service_uuid),
                         (uint8_t *)nus_service_uuid}
                    },
                    // RX characteristic declaration
                    [GATTS_ATTR_RX_DECL] = {
                        {ESP_GATT_AUTO_RSP},
                        {ESP_UUID_LEN_16, (uint8_t *)&(uint16_t){ESP_GATT_UUID_CHAR_DECLARE},
                         ESP_GATT_PERM_READ, sizeof(uint8_t), 1,
                         (uint8_t *)&(uint8_t){
                             ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR
                         }}
                    },
                    // RX characteristic value
                    [GATTS_ATTR_RX_VAL] = {
                        {ESP_GATT_AUTO_RSP},
                        {ESP_UUID_LEN_128, (uint8_t *)nus_rx_char_uuid,
                         ESP_GATT_PERM_WRITE, LINE_BUF_SIZE, 0, NULL}
                    },
                    // TX characteristic declaration
                    [GATTS_ATTR_TX_DECL] = {
                        {ESP_GATT_AUTO_RSP},
                        {ESP_UUID_LEN_16, (uint8_t *)&(uint16_t){ESP_GATT_UUID_CHAR_DECLARE},
                         ESP_GATT_PERM_READ, sizeof(uint8_t), 1,
                         (uint8_t *)&(uint8_t){
                             ESP_GATT_CHAR_PROP_BIT_NOTIFY
                         }}
                    },
                    // TX characteristic value
                    [GATTS_ATTR_TX_VAL] = {
                        {ESP_GATT_AUTO_RSP},
                        {ESP_UUID_LEN_128, (uint8_t *)nus_tx_char_uuid,
                         0, 0, 0, NULL}
                    },
                    // TX client characteristic configuration descriptor (CCCD)
                    [GATTS_ATTR_TX_CCCD] = {
                        {ESP_GATT_AUTO_RSP},
                        {ESP_UUID_LEN_16, (uint8_t *)&(uint16_t){ESP_GATT_UUID_CHAR_CLIENT_CONFIG},
                         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                         sizeof(uint16_t), 0, NULL}
                    },
                },
                gatts_if,
                GATTS_NUM_HANDLES,
                app_id);
            break;
        }

        case ESP_GATTS_CREAT_ATTR_TAB_EVT: {
            if (param->add_attr_tab.status != ESP_GATT_OK) {
                ESP_LOGE(TAG, "Create attr tab failed: %d", param->add_attr_tab.status);
                break;
            }
            if (param->add_attr_tab.svc_uuid.len == ESP_UUID_LEN_16 &&
                param->add_attr_tab.svc_uuid.uuid.uuid16 == ESP_GATT_UUID_PRI_SERVICE) {
                // Extract handles from the created table
                // The handles are assigned sequentially
                rx_handle = param->add_attr_tab.handles[2];   // RX value
                tx_handle = param->add_attr_tab.handles[4];   // TX value
                tx_ccc_handle = param->add_attr_tab.handles[5]; // TX CCCD

                ESP_LOGI(TAG, "Attr table created, RX=%d, TX=%d, CCC=%d",
                         rx_handle, tx_handle, tx_ccc_handle);

                // Start the service
                uint16_t svc_handle = param->add_attr_tab.handles[0];
                esp_ble_gatts_start_service(svc_handle);
                service_created = true;
            }
            break;
        }

        case ESP_GATTS_START_EVT: {
            if (param->start.status == ESP_GATT_OK) {
                ESP_LOGI(TAG, "Service started");
                start_advertising();
            } else {
                ESP_LOGE(TAG, "Service start failed: %d", param->start.status);
            }
            break;
        }

        case ESP_GATTS_CONNECT_EVT: {
            ESP_LOGI(TAG, "Client connected, conn_id=%d", param->connect.conn_id);
            is_connected = true;
            conn_id = param->connect.conn_id;
            tx_notify_enabled = false;

            // Stop advertising
            esp_ble_gap_stop_advertising();

            // Request connection parameter update
            esp_ble_conn_update_params_t conn_params = {0};
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            conn_params.min_int = 0x06;   // 7.5ms
            conn_params.max_int = 0x0C;   // 15ms
            conn_params.latency = 0;
            conn_params.timeout = 400;     // 4s
            esp_ble_gap_update_conn_params(&conn_params);

            invoke_on_connected();
            break;
        }

        case ESP_GATTS_DISCONNECT_EVT: {
            ESP_LOGI(TAG, "Client disconnected, reason=0x%x", param->disconnect.reason);
            is_connected = false;
            tx_notify_enabled = false;
            line_len = 0;

            invoke_on_disconnected();

            // Restart advertising
            start_advertising();
            break;
        }

        case ESP_GATTS_WRITE_EVT: {
            if (param->write.handle == tx_ccc_handle) {
                // CCCD write for TX notifications
                if (param->write.len == 2) {
                    uint16_t ccc_val = param->write.value[0] | (param->write.value[1] << 8);
                    tx_notify_enabled = (ccc_val == 0x0001);  // 0x0001 = notification enabled
                    ESP_LOGI(TAG, "TX notify %s", tx_notify_enabled ? "enabled" : "disabled");
                }
            } else if (param->write.handle == rx_handle) {
                // RX data received - accumulate in line buffer
                int write_len = param->write.len;
                const uint8_t *write_data = param->write.value;

                if (write_len <= 0) break;

                for (int i = 0; i < write_len; i++) {
                    char c = (char)write_data[i];
                    if (c == '\n') {
                        // Complete line received
                        if (line_len > 0) {
                            parse_json_line(line_buf, line_len);
                            line_len = 0;
                        }
                    } else if (c == '\r') {
                        // Ignore carriage return
                        continue;
                    } else {
                        if (line_len < LINE_BUF_SIZE - 1) {
                            line_buf[line_len++] = c;
                        } else {
                            ESP_LOGW(TAG, "Line buffer overflow, resetting");
                            line_len = 0;
                        }
                    }
                }
            }
            break;
        }

        case ESP_GATTS_CONF_EVT: {
            ESP_LOGD(TAG, "Conf event, status=%d", param->conf.status);
            break;
        }

        default:
            break;
    }
}

// ---- Public API ----

int ble_service_init(void)
{
    esp_err_t ret;

    // Generate device name with last 2 bytes of MAC
    uint8_t mac[6];
    ret = esp_read_mac(mac, ESP_MAC_BT);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read BT MAC, using BT addr");
    }
    snprintf(device_name, sizeof(device_name), DEVICE_NAME_PREFIX "%02X%02X",
             mac[4], mac[5]);
    ESP_LOGI(TAG, "Device name: %s", device_name);

    // Initialize Bluetooth controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(ret));
        return -1;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
        return -1;
    }

    // Initialize Bluedroid stack
    esp_bluedroid_config_t bluedroid_cfg = {
        .ssp_en = true,
        .sc_en = false,
    };
    ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return -1;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return -1;
    }

    // Set MTU to a reasonable size
    ret = esp_ble_gatt_set_local_mtu(512);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Set local MTU failed: %s", esp_err_to_name(ret));
    }

    // Register callbacks
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTS callback register failed: %s", esp_err_to_name(ret));
        return -1;
    }

    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP callback register failed: %s", esp_err_to_name(ret));
        return -1;
    }

    // Register GATT application
    ret = esp_ble_gatts_app_register(app_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATT app register failed: %s", esp_err_to_name(ret));
        return -1;
    }

    ESP_LOGI(TAG, "BLE service initialized");
    return 0;
}

void ble_service_register_callbacks(const ble_callbacks_t *cbs)
{
    if (cbs) {
        callbacks = *cbs;
    }
}

void ble_service_send_permission(const char *id, const char *decision)
{
    if (!is_connected) return;

    char buf[256];
    int len = snprintf(buf, sizeof(buf),
                       "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"%s\"}\n",
                       id, decision);
    if (len > 0 && len < (int)sizeof(buf)) {
        send_tx_data(buf, len);
        ESP_LOGI(TAG, "Sent permission: id=%s decision=%s", id, decision);
    }
}

void ble_service_send_ack(const char *cmd, bool ok)
{
    if (!is_connected) return;

    char buf[128];
    int len = snprintf(buf, sizeof(buf),
                       "{\"ack\":\"%s\",\"ok\":%s}\n",
                       cmd, ok ? "true" : "false");
    if (len > 0 && len < (int)sizeof(buf)) {
        send_tx_data(buf, len);
    }
}

bool ble_service_is_connected(void)
{
    return is_connected;
}

void ble_service_tick(void)
{
    // No-op for now (future: heartbeat timeout detection)
}
