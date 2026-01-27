/**
 * @file ble_service.c
 * @brief BLE GATT Service Implementation for RS232-MQTT Configuration
 * 
 * Fixed UUID length issues for proper GATT service discovery
 */

#include "ble_service.h"
#include "protocol_def.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "BLE_SERVICE";

#define PROFILE_NUM         1
#define PROFILE_APP_IDX     0
#define ESP_APP_ID          0x55
#define SVC_INST_ID         0

// Characteristic indices
enum {
    IDX_SVC,
    IDX_CHAR_WIFI_CFG,
    IDX_CHAR_WIFI_CFG_VAL,
    IDX_CHAR_MQTT_CFG,
    IDX_CHAR_MQTT_CFG_VAL,
    IDX_CHAR_PROTOCOL_CFG,
    IDX_CHAR_PROTOCOL_CFG_VAL,
    IDX_CHAR_UART_CFG,
    IDX_CHAR_UART_CFG_VAL,
    IDX_CHAR_DATA_DEF,
    IDX_CHAR_DATA_DEF_VAL,
    IDX_CHAR_STATUS,
    IDX_CHAR_STATUS_VAL,
    IDX_CHAR_STATUS_CCC,
    IDX_CHAR_PARSED_DATA,
    IDX_CHAR_PARSED_DATA_VAL,
    IDX_CHAR_PARSED_DATA_CCC,
    IDX_CHAR_COMMAND,
    IDX_CHAR_COMMAND_VAL,
    IDX_NB,
};

// Service UUID: 4fafc201-1fb5-459e-8fcc-c5c9c331914b (Little Endian)
static uint8_t svc_uuid[16] = {
    0x4b, 0x91, 0x31, 0xc3, 0xc9, 0xc5, 0xcc, 0x8f,
    0x9e, 0x45, 0xb5, 0x1f, 0x01, 0xc2, 0xaf, 0x4f
};

// Characteristic UUIDs (128-bit, Little Endian)
// beb5483e-36e1-4688-b7f5-ea07361b26a8 (WiFi)
static uint8_t char_wifi_uuid[16] = {
    0xa8, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7,
    0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe
};
// beb5483e-36e1-4688-b7f5-ea07361b26a9 (MQTT)
static uint8_t char_mqtt_uuid[16] = {
    0xa9, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7,
    0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe
};
// beb5483e-36e1-4688-b7f5-ea07361b26aa (Protocol)
static uint8_t char_protocol_uuid[16] = {
    0xaa, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7,
    0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe
};
// beb5483e-36e1-4688-b7f5-ea07361b26ab (UART)
static uint8_t char_uart_uuid[16] = {
    0xab, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7,
    0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe
};
// beb5483e-36e1-4688-b7f5-ea07361b26ac (Data Definition)
static uint8_t char_datadef_uuid[16] = {
    0xac, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7,
    0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe
};
// beb5483e-36e1-4688-b7f5-ea07361b26ad (Status)
static uint8_t char_status_uuid[16] = {
    0xad, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7,
    0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe
};
// beb5483e-36e1-4688-b7f5-ea07361b26ae (Parsed Data)
static uint8_t char_parsed_uuid[16] = {
    0xae, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7,
    0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe
};
// beb5483e-36e1-4688-b7f5-ea07361b26af (Command)
static uint8_t char_command_uuid[16] = {
    0xaf, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7,
    0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe
};

// Standard 16-bit UUIDs
static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t char_decl_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t char_ccc_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

// Characteristic properties
static const uint8_t char_prop_write = ESP_GATT_CHAR_PROP_BIT_WRITE;
static const uint8_t char_prop_read_notify = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t char_prop_notify = ESP_GATT_CHAR_PROP_BIT_NOTIFY;

// CCC values
static uint8_t status_ccc[2] = {0x00, 0x00};
static uint8_t parsed_ccc[2] = {0x00, 0x00};

// Dummy value for write-only characteristics (must have non-null value for AUTO_RSP)
static uint8_t dummy_value[1] = {0x00};

static uint16_t s_handle_table[IDX_NB];
static uint16_t s_conn_id = 0xFFFF;
static bool s_is_connected = false;
static uint16_t s_mtu = 23;
static esp_gatt_if_t s_gatts_if = ESP_GATT_IF_NONE;
static ble_cmd_cb_t s_command_callback = NULL;
static char s_device_name[32] = "RS232_MQTT_Bridge";

// Advertising data
static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(svc_uuid),
    .p_service_uuid = svc_uuid,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = true,
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// GATT Database - CRITICAL: Use ESP_UUID_LEN_128 for 128-bit UUIDs!
static const esp_gatts_attr_db_t gatt_db[IDX_NB] = {
    // Service Declaration
    [IDX_SVC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t*)&primary_service_uuid, ESP_GATT_PERM_READ,
         sizeof(svc_uuid), sizeof(svc_uuid), svc_uuid}
    },

    // WiFi Config Characteristic
    [IDX_CHAR_WIFI_CFG] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t*)&char_decl_uuid, ESP_GATT_PERM_READ,
         sizeof(char_prop_write), sizeof(char_prop_write), (uint8_t*)&char_prop_write}
    },
    [IDX_CHAR_WIFI_CFG_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, char_wifi_uuid, ESP_GATT_PERM_WRITE,
         512, sizeof(dummy_value), dummy_value}
    },

    // MQTT Config Characteristic
    [IDX_CHAR_MQTT_CFG] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t*)&char_decl_uuid, ESP_GATT_PERM_READ,
         sizeof(char_prop_write), sizeof(char_prop_write), (uint8_t*)&char_prop_write}
    },
    [IDX_CHAR_MQTT_CFG_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, char_mqtt_uuid, ESP_GATT_PERM_WRITE,
         512, sizeof(dummy_value), dummy_value}
    },

    // Protocol Config Characteristic
    [IDX_CHAR_PROTOCOL_CFG] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t*)&char_decl_uuid, ESP_GATT_PERM_READ,
         sizeof(char_prop_write), sizeof(char_prop_write), (uint8_t*)&char_prop_write}
    },
    [IDX_CHAR_PROTOCOL_CFG_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, char_protocol_uuid, ESP_GATT_PERM_WRITE,
         512, sizeof(dummy_value), dummy_value}
    },

    // UART Config Characteristic
    [IDX_CHAR_UART_CFG] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t*)&char_decl_uuid, ESP_GATT_PERM_READ,
         sizeof(char_prop_write), sizeof(char_prop_write), (uint8_t*)&char_prop_write}
    },
    [IDX_CHAR_UART_CFG_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, char_uart_uuid, ESP_GATT_PERM_WRITE,
         512, sizeof(dummy_value), dummy_value}
    },

    // Data Definition Characteristic
    [IDX_CHAR_DATA_DEF] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t*)&char_decl_uuid, ESP_GATT_PERM_READ,
         sizeof(char_prop_write), sizeof(char_prop_write), (uint8_t*)&char_prop_write}
    },
    [IDX_CHAR_DATA_DEF_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, char_datadef_uuid, ESP_GATT_PERM_WRITE,
         512, sizeof(dummy_value), dummy_value}
    },

    // Status Characteristic (Read + Notify)
    [IDX_CHAR_STATUS] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t*)&char_decl_uuid, ESP_GATT_PERM_READ,
         sizeof(char_prop_read_notify), sizeof(char_prop_read_notify), (uint8_t*)&char_prop_read_notify}
    },
    [IDX_CHAR_STATUS_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, char_status_uuid, ESP_GATT_PERM_READ,
         sizeof(device_status_t), sizeof(dummy_value), dummy_value}
    },
    [IDX_CHAR_STATUS_CCC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t*)&char_ccc_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         sizeof(status_ccc), sizeof(status_ccc), status_ccc}
    },

    // Parsed Data Characteristic (Notify)
    [IDX_CHAR_PARSED_DATA] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t*)&char_decl_uuid, ESP_GATT_PERM_READ,
         sizeof(char_prop_notify), sizeof(char_prop_notify), (uint8_t*)&char_prop_notify}
    },
    [IDX_CHAR_PARSED_DATA_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, char_parsed_uuid, ESP_GATT_PERM_READ,
         512, sizeof(dummy_value), dummy_value}
    },
    [IDX_CHAR_PARSED_DATA_CCC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t*)&char_ccc_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         sizeof(parsed_ccc), sizeof(parsed_ccc), parsed_ccc}
    },

    // Command Characteristic
    [IDX_CHAR_COMMAND] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t*)&char_decl_uuid, ESP_GATT_PERM_READ,
         sizeof(char_prop_write), sizeof(char_prop_write), (uint8_t*)&char_prop_write}
    },
    [IDX_CHAR_COMMAND_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, char_command_uuid, ESP_GATT_PERM_WRITE,
         512, sizeof(dummy_value), dummy_value}
    },
};

static void process_write_event(uint16_t handle, const uint8_t *data, uint16_t len)
{
    if (len < 5) {
        ESP_LOGW(TAG, "Packet too short: %d bytes", len);
        return;
    }

    if (data[0] != PACKET_STX) {
        ESP_LOGW(TAG, "Invalid STX: 0x%02X", data[0]);
        return;
    }

    uint8_t cmd = data[1];
    uint16_t payload_len = data[2] | (data[3] << 8);

    if (len < (size_t)(4 + payload_len + 2)) {
        ESP_LOGW(TAG, "Incomplete packet");
        return;
    }

    const uint8_t *payload = &data[4];
    
    // Verify ETX
    if (data[4 + payload_len + 1] != PACKET_ETX) {
        ESP_LOGW(TAG, "Invalid ETX");
        return;
    }

    ESP_LOGI(TAG, "CMD: 0x%02X, Len: %d", cmd, payload_len);

    // Call command handler callback
    if (s_command_callback) {
        s_command_callback(cmd, payload, payload_len);
    }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            esp_ble_gap_start_advertising(&adv_params);
            break;

        case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
            esp_ble_gap_start_advertising(&adv_params);
            break;

        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Advertising started");
            }
            break;

        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            ESP_LOGD(TAG, "Conn params updated");
            break;

        default:
            break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                 esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
        case ESP_GATTS_REG_EVT:
            if (param->reg.status == ESP_GATT_OK) {
                s_gatts_if = gatts_if;
                esp_ble_gap_set_device_name(s_device_name);
                esp_ble_gap_config_adv_data(&adv_data);
                esp_ble_gap_config_adv_data(&scan_rsp_data);
                
                esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, IDX_NB, SVC_INST_ID);
            } else {
                ESP_LOGE(TAG, "GATTS reg failed: %d", param->reg.status);
            }
            break;

        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            if (param->add_attr_tab.status == ESP_GATT_OK) {
                if (param->add_attr_tab.num_handle == IDX_NB) {
                    memcpy(s_handle_table, param->add_attr_tab.handles, sizeof(s_handle_table));
                    esp_ble_gatts_start_service(s_handle_table[IDX_SVC]);
                    ESP_LOGI(TAG, "GATT table created, %d handles", IDX_NB);
                } else {
                    ESP_LOGE(TAG, "Handle count mismatch: %d vs %d", 
                             param->add_attr_tab.num_handle, IDX_NB);
                }
            } else {
                ESP_LOGE(TAG, "GATT table create failed: %d", param->add_attr_tab.status);
            }
            break;

        case ESP_GATTS_START_EVT:
            if (param->start.status == ESP_GATT_OK) {
                ESP_LOGI(TAG, "Service started");
            }
            break;

        case ESP_GATTS_CONNECT_EVT:
            s_conn_id = param->connect.conn_id;
            s_is_connected = true;
            ESP_LOGI(TAG, "BLE connected, conn_id=%d", s_conn_id);
            
            // Update connection parameters for better stability
            esp_ble_conn_update_params_t conn_params = {0};
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            conn_params.latency = 0;
            conn_params.max_int = 0x20;    // 40ms
            conn_params.min_int = 0x10;    // 20ms
            conn_params.timeout = 400;     // 4s
            esp_ble_gap_update_conn_params(&conn_params);
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            s_is_connected = false;
            s_conn_id = 0xFFFF;
            s_mtu = 23;
            ESP_LOGI(TAG, "BLE disconnected, reason=0x%x", param->disconnect.reason);
            esp_ble_gap_start_advertising(&adv_params);
            break;

        case ESP_GATTS_MTU_EVT:
            s_mtu = param->mtu.mtu;
            ESP_LOGI(TAG, "MTU: %d", s_mtu);
            break;

        case ESP_GATTS_WRITE_EVT:
            if (!param->write.is_prep) {
                ESP_LOGD(TAG, "Write to handle %d, len=%d", param->write.handle, param->write.len);
                process_write_event(param->write.handle, param->write.value, param->write.len);
                
                // Send write response if needed
                if (param->write.need_rsp) {
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                                param->write.trans_id, ESP_GATT_OK, NULL);
                }
            }
            break;

        case ESP_GATTS_READ_EVT:
            ESP_LOGD(TAG, "Read handle %d", param->read.handle);
            // AUTO_RSP handles reads automatically
            break;

        case ESP_GATTS_CONF_EVT:
            // Notification/indication confirmation
            break;

        default:
            break;
    }
}

/*******************************************************************************
 * Public Functions
 ******************************************************************************/
esp_err_t ble_service_init(const char *device_name)
{
    ESP_LOGI(TAG, "Initializing BLE...");

    if (device_name) {
        strncpy(s_device_name, device_name, sizeof(s_device_name) - 1);
        s_device_name[sizeof(s_device_name) - 1] = '\0';
    }

    esp_err_t ret;

    // Release classic BT memory
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    // Initialize BT controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize Bluedroid
    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register callbacks
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "GATTS register callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "GAP register callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register GATT application
    ret = esp_ble_gatts_app_register(ESP_APP_ID);
    if (ret) {
        ESP_LOGE(TAG, "GATTS app register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set MTU
    esp_ble_gatt_set_local_mtu(500);

    ESP_LOGI(TAG, "BLE initialized");
    return ESP_OK;
}

void ble_service_start(void)
{
    esp_ble_gap_start_advertising(&adv_params);
}

void ble_service_stop(void)
{
    esp_ble_gap_stop_advertising();
}

bool ble_service_is_connected(void)
{
    return s_is_connected;
}

void ble_service_set_callback(ble_cmd_cb_t callback)
{
    s_command_callback = callback;
}

esp_err_t ble_service_send_ack(uint8_t original_cmd, uint8_t result)
{
    if (!s_is_connected || s_gatts_if == ESP_GATT_IF_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    // Build ACK packet
    uint8_t packet[8];
    packet[0] = PACKET_STX;
    packet[1] = RSP_ACK;
    packet[2] = 2;  // Payload length (low byte)
    packet[3] = 0;  // Payload length (high byte)
    packet[4] = original_cmd;
    packet[5] = result;
    packet[6] = packet[1] ^ packet[4] ^ packet[5];  // Simple XOR checksum
    packet[7] = PACKET_ETX;

    esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id,
                                 s_handle_table[IDX_CHAR_STATUS_VAL],
                                 sizeof(packet), packet, false);
    return ESP_OK;
}

esp_err_t ble_service_notify_status(const device_status_t *status)
{
    if (!s_is_connected || s_gatts_if == ESP_GATT_IF_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    // Build status notification packet
    uint8_t packet[32];
    uint16_t offset = 0;

    packet[offset++] = PACKET_STX;
    packet[offset++] = RSP_STATUS;
    packet[offset++] = sizeof(device_status_t);  // Low byte
    packet[offset++] = 0;  // High byte

    memcpy(&packet[offset], status, sizeof(device_status_t));
    offset += sizeof(device_status_t);

    // Simple checksum
    uint8_t crc = 0;
    for (int i = 1; i < offset; i++) {
        crc ^= packet[i];
    }
    packet[offset++] = crc;
    packet[offset++] = PACKET_ETX;

    esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id,
                                 s_handle_table[IDX_CHAR_STATUS_VAL],
                                 offset, packet, false);
    return ESP_OK;
}

esp_err_t ble_service_notify_data(const uint8_t *data, uint16_t length)
{
    if (!s_is_connected || s_gatts_if == ESP_GATT_IF_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    // Limit to MTU - 3 (ATT overhead)
    uint16_t max_len = s_mtu - 3;
    if (length > max_len) {
        length = max_len;
    }

    esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id,
                                 s_handle_table[IDX_CHAR_PARSED_DATA_VAL],
                                 length, (uint8_t*)data, false);
    return ESP_OK;
}

uint16_t ble_service_get_mtu(void)
{
    return s_mtu;
}
