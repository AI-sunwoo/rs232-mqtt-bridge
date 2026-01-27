/**
 * @file cmd_handler.c
 * @brief BLE Command Handler Implementation
 */

#include "cmd_handler.h"
#include "protocol_def.h"
#include "nvs_storage.h"
#include "wifi_manager.h"
#include "mqtt_handler.h"
#include "uart_handler.h"
#include "data_parser.h"
#include "ble_service.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "CMD_HANDLER";

// External references
extern wifi_config_data_t g_wifi_config;
extern mqtt_config_data_t g_mqtt_config;
extern uart_config_data_t g_uart_config;
extern protocol_config_data_t g_protocol_config;
extern data_definition_t g_data_definition;
extern device_status_t g_device_status;

/*******************************************************************************
 * Configuration Parsers
 ******************************************************************************/
esp_err_t cmd_parse_wifi_config(const uint8_t *data, uint16_t len, wifi_config_data_t *config)
{
    if (len < 2 || !config) return ESP_ERR_INVALID_ARG;

    memset(config, 0, sizeof(wifi_config_data_t));
    uint8_t offset = 0;

    // SSID length
    uint8_t ssid_len = data[offset++];
    if (ssid_len > WIFI_SSID_MAX_LEN || offset + ssid_len > len) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(config->ssid, &data[offset], ssid_len);
    offset += ssid_len;

    // Password length
    if (offset >= len) return ESP_ERR_INVALID_ARG;
    uint8_t pwd_len = data[offset++];
    if (pwd_len > WIFI_PASSWORD_MAX_LEN || offset + pwd_len > len) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(config->password, &data[offset], pwd_len);

    ESP_LOGI(TAG, "WiFi config parsed: SSID=%s", config->ssid);
    return ESP_OK;
}

esp_err_t cmd_parse_mqtt_config(const uint8_t *data, uint16_t len, mqtt_config_data_t *config)
{
    if (len < 4 || !config) return ESP_ERR_INVALID_ARG;

    memset(config, 0, sizeof(mqtt_config_data_t));
    uint16_t offset = 0;

    // Broker
    uint8_t broker_len = data[offset++];
    if (broker_len > MQTT_BROKER_MAX_LEN || offset + broker_len > len) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(config->broker, &data[offset], broker_len);
    offset += broker_len;

    // Port
    if (offset + 2 > len) return ESP_ERR_INVALID_ARG;
    config->port = data[offset] | (data[offset + 1] << 8);
    offset += 2;

    // Username
    if (offset >= len) return ESP_ERR_INVALID_ARG;
    uint8_t user_len = data[offset++];
    if (offset + user_len > len) return ESP_ERR_INVALID_ARG;
    memcpy(config->username, &data[offset], user_len);
    offset += user_len;

    // Password
    if (offset >= len) return ESP_ERR_INVALID_ARG;
    uint8_t pwd_len = data[offset++];
    if (offset + pwd_len > len) return ESP_ERR_INVALID_ARG;
    memcpy(config->password, &data[offset], pwd_len);
    offset += pwd_len;

    // Client ID
    if (offset >= len) return ESP_ERR_INVALID_ARG;
    uint8_t cid_len = data[offset++];
    if (offset + cid_len > len) return ESP_ERR_INVALID_ARG;
    memcpy(config->client_id, &data[offset], cid_len);
    offset += cid_len;

    // Topic
    if (offset >= len) return ESP_ERR_INVALID_ARG;
    uint8_t topic_len = data[offset++];
    if (offset + topic_len > len) return ESP_ERR_INVALID_ARG;
    memcpy(config->topic, &data[offset], topic_len);
    offset += topic_len;

    // QoS
    if (offset >= len) return ESP_ERR_INVALID_ARG;
    config->qos = data[offset++];
    if (config->qos > 2) config->qos = 1;

    // TLS
    if (offset < len) {
        config->use_tls = (data[offset] != 0);
    }

    ESP_LOGI(TAG, "MQTT config parsed: broker=%s:%d", config->broker, config->port);
    return ESP_OK;
}

esp_err_t cmd_parse_uart_config(const uint8_t *data, uint16_t len, uart_config_data_t *config)
{
    if (len < 8 || !config) return ESP_ERR_INVALID_ARG;

    config->baudrate = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    config->data_bits = (data[4] == 7) ? 7 : 8;
    config->parity = (data[5] <= 2) ? data[5] : 0;
    config->stop_bits = (data[6] == 2) ? 2 : 1;
    config->flow_control = (data[7] <= 2) ? data[7] : 0;

    ESP_LOGI(TAG, "UART config parsed: %lu-%d-%d-%d",
             (unsigned long)config->baudrate, config->data_bits,
             config->parity, config->stop_bits);
    return ESP_OK;
}

esp_err_t cmd_parse_protocol_config(const uint8_t *data, uint16_t len, protocol_config_data_t *config)
{
    if (len < 3 || !config) return ESP_ERR_INVALID_ARG;

    memset(config, 0, sizeof(protocol_config_data_t));
    config->type = (protocol_type_t)data[0];

    uint16_t config_len = data[1] | (data[2] << 8);
    const uint8_t *config_data = &data[3];

    if (3 + config_len > len) return ESP_ERR_INVALID_ARG;

    switch (config->type) {
        case PROTOCOL_CUSTOM:
            if (config_len >= sizeof(custom_protocol_config_t)) {
                memcpy(&config->config.custom, config_data, sizeof(custom_protocol_config_t));
            }
            break;
        case PROTOCOL_MODBUS_RTU:
        case PROTOCOL_MODBUS_ASCII:
            if (config_len >= sizeof(modbus_rtu_config_t)) {
                memcpy(&config->config.modbus_rtu, config_data, sizeof(modbus_rtu_config_t));
            }
            break;
        case PROTOCOL_NMEA_0183:
            if (config_len >= 3) {
                config->config.nmea.sentence_filter_count = config_data[0];
                config->config.nmea.validate_checksum = true;
            }
            break;
        case PROTOCOL_IEC_60870_101:
        case PROTOCOL_IEC_60870_104:
            if (config_len >= sizeof(iec60870_config_t)) {
                memcpy(&config->config.iec60870, config_data, sizeof(iec60870_config_t));
            }
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Protocol config parsed: type=%d", config->type);
    return ESP_OK;
}

esp_err_t cmd_parse_data_definition(const uint8_t *data, uint16_t len, data_definition_t *def)
{
    if (len < 2 || !def) return ESP_ERR_INVALID_ARG;

    memset(def, 0, sizeof(data_definition_t));
    def->field_count = data[0];
    def->data_offset = data[1];

    if (def->field_count > MAX_FIELD_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t offset = 2;

    // Parse field definitions
    for (uint8_t i = 0; i < def->field_count && offset + sizeof(field_definition_t) <= len; i++) {
        memcpy(&def->fields[i], &data[offset], sizeof(field_definition_t));
        offset += sizeof(field_definition_t);
    }

    // Parse field names
    if (offset < len) {
        def->names_length = len - offset;
        if (def->names_length > MAX_FIELD_NAMES_SIZE) {
            def->names_length = MAX_FIELD_NAMES_SIZE;
        }
        memcpy(def->field_names, &data[offset], def->names_length);
    }

    ESP_LOGI(TAG, "Data definition parsed: %d fields", def->field_count);
    return ESP_OK;
}

/*******************************************************************************
 * WiFi Connect Task
 ******************************************************************************/
static void wifi_connect_task(void *arg)
{
    wifi_config_data_t *config = (wifi_config_data_t *)arg;
    wifi_manager_connect(config);
    vTaskDelete(NULL);
}

/*******************************************************************************
 * Command Handler
 ******************************************************************************/
void cmd_handler_process(cmd_code_t cmd, const uint8_t *data, uint16_t len)
{
    esp_err_t ret = ESP_OK;
    result_code_t result = RESULT_SUCCESS;

    ESP_LOGI(TAG, "Processing command: 0x%02X", cmd);

    switch (cmd) {
        case CMD_SET_WIFI:
            ret = cmd_parse_wifi_config(data, len, &g_wifi_config);
            if (ret == ESP_OK) {
                nvs_save_wifi_config(&g_wifi_config);
                xTaskCreate(wifi_connect_task, "wifi_conn", 4096, &g_wifi_config, 3, NULL);
            }
            break;

        case CMD_SET_MQTT:
            ret = cmd_parse_mqtt_config(data, len, &g_mqtt_config);
            if (ret == ESP_OK) {
                nvs_save_mqtt_config(&g_mqtt_config);
                if (wifi_manager_is_connected()) {
                    mqtt_handler_stop();
                    mqtt_handler_start(&g_mqtt_config);
                }
            }
            break;

        case CMD_SET_UART:
            ret = cmd_parse_uart_config(data, len, &g_uart_config);
            if (ret == ESP_OK) {
                nvs_save_uart_config(&g_uart_config);
                uart_handler_stop();
                uart_handler_start(&g_uart_config, &g_protocol_config);
            }
            break;

        case CMD_SET_PROTOCOL:
            ret = cmd_parse_protocol_config(data, len, &g_protocol_config);
            if (ret == ESP_OK) {
                nvs_save_protocol_config(&g_protocol_config);
                uart_handler_update_protocol(&g_protocol_config);
            }
            break;

        case CMD_SET_DATA_DEF:
            ret = cmd_parse_data_definition(data, len, &g_data_definition);
            if (ret == ESP_OK) {
                nvs_save_data_definition(&g_data_definition);
                data_parser_set_definition(&g_data_definition);
            }
            break;

        case CMD_GET_STATUS:
            ble_service_notify_status(&g_device_status);
            break;

        case CMD_SAVE_CONFIG:
            ESP_LOGI(TAG, "Configuration saved");
            break;

        case CMD_RESET_CONFIG:
            ESP_LOGW(TAG, "Factory reset");
            nvs_reset_to_defaults();
            esp_restart();
            break;

        case CMD_START_MONITOR:
            ESP_LOGI(TAG, "Monitoring started");
            break;

        case CMD_STOP_MONITOR:
            ESP_LOGI(TAG, "Monitoring stopped");
            break;

        default:
            ESP_LOGW(TAG, "Unknown command: 0x%02X", cmd);
            ret = ESP_ERR_INVALID_ARG;
            break;
    }

    if (ret != ESP_OK) {
        result = (ret == ESP_ERR_INVALID_ARG) ? RESULT_INVALID : RESULT_FAILED;
    }
    ble_service_send_ack(cmd, result);
}
