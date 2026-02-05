/**
 * @file cmd_handler.c
 * @brief BLE Command Handler Implementation
 * @version 3.0.0
 * @date 2026-02-04
 * 
 * 통합 인터페이스 정의서 v3.0 기반
 * S2-3: main.c 중복 파서 제거, 이 파일이 유일한 파싱 소스
 * P0-1: user_id, device_id, base_topic 파싱
 * P0-2: use_jwt 플래그 파싱
 */

#include "cmd_handler.h"
#include "protocol_def.h"
#include "nvs_storage.h"
#include "wifi_manager.h"
#include "mqtt_handler.h"
#include "uart_handler.h"
#include "data_parser.h"
#include "ble_service.h"
#include "cJSON.h"
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
 * WiFi Configuration Parser (Section 4.1)
 ******************************************************************************/
esp_err_t cmd_parse_wifi_config(const uint8_t *data, uint16_t len, wifi_config_data_t *config)
{
    if (len < 2 || !config) return ESP_ERR_INVALID_ARG;

    memset(config, 0, sizeof(wifi_config_data_t));
    uint16_t offset = 0;

    // SSID length
    uint8_t ssid_len = data[offset++];
    if (ssid_len > WIFI_SSID_MAX_LEN || offset + ssid_len > len) {
        ESP_LOGE(TAG, "Invalid SSID length: %d", ssid_len);
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(config->ssid, &data[offset], ssid_len);
    offset += ssid_len;

    // Password length
    if (offset >= len) return ESP_ERR_INVALID_ARG;
    uint8_t pwd_len = data[offset++];
    if (pwd_len > WIFI_PASSWORD_MAX_LEN || offset + pwd_len > len) {
        ESP_LOGE(TAG, "Invalid password length: %d", pwd_len);
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(config->password, &data[offset], pwd_len);

    ESP_LOGI(TAG, "WiFi config parsed: SSID=%s", config->ssid);
    return ESP_OK;
}

/*******************************************************************************
 * MQTT Configuration Parser (Section 4.2) - P0-1, P0-2 수정
 * 
 * v2.1 패킷 구조:
 * - broker_len(1) + broker(N)
 * - port(2, LE)
 * - username_len(1) + username(M)
 * - password_len(2, LE) - v2.1: JWT 지원을 위해 2바이트로 변경
 * - password(P)
 * - client_id_len(1) + client_id(Q)
 * - user_id_len(1) + user_id(R) - P0-1: 필수
 * - device_id_len(1) + device_id(S) - P0-1: 필수
 * - base_topic_len(1) + base_topic(T) - P0-1: 필수
 * - qos(1)
 * - use_tls(1)
 * - use_jwt(1) - P0-2: JWT 인증 플래그
 ******************************************************************************/
esp_err_t cmd_parse_mqtt_config(const uint8_t *data, uint16_t len, mqtt_config_data_t *config)
{
    if (len < 4 || !config) return ESP_ERR_INVALID_ARG;

    memset(config, 0, sizeof(mqtt_config_data_t));
    config->port = DEFAULT_MQTT_PORT;
    config->qos = DEFAULT_MQTT_QOS;
    config->use_tls = false;
    config->use_jwt = false;
    
    uint16_t offset = 0;

    // ========== Broker ==========
    if (offset >= len) return ESP_ERR_INVALID_ARG;
    uint8_t broker_len = data[offset++];
    if (broker_len > MQTT_BROKER_MAX_LEN || offset + broker_len > len) {
        ESP_LOGE(TAG, "Invalid broker length: %d", broker_len);
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(config->broker, &data[offset], broker_len);
    offset += broker_len;

    // ========== Port (2 bytes, Little Endian) ==========
    if (offset + 2 > len) return ESP_ERR_INVALID_ARG;
    config->port = data[offset] | (data[offset + 1] << 8);
    offset += 2;

    // ========== Username ==========
    if (offset >= len) return ESP_ERR_INVALID_ARG;
    uint8_t user_len = data[offset++];
    if (offset + user_len > len) return ESP_ERR_INVALID_ARG;
    if (user_len > MQTT_USERNAME_MAX_LEN) user_len = MQTT_USERNAME_MAX_LEN;
    memcpy(config->username, &data[offset], user_len);
    offset += user_len;

    // ========== Password (v2.1: 2-byte length for JWT support) ==========
    if (offset + 2 > len) return ESP_ERR_INVALID_ARG;
    uint16_t pwd_len = data[offset] | (data[offset + 1] << 8);
    offset += 2;
    if (offset + pwd_len > len) return ESP_ERR_INVALID_ARG;
    if (pwd_len > MQTT_PASSWORD_MAX_LEN) pwd_len = MQTT_PASSWORD_MAX_LEN;
    memcpy(config->password, &data[offset], pwd_len);
    offset += pwd_len;

    // ========== Client ID ==========
    if (offset >= len) return ESP_ERR_INVALID_ARG;
    uint8_t cid_len = data[offset++];
    if (offset + cid_len > len) return ESP_ERR_INVALID_ARG;
    if (cid_len > MQTT_CLIENT_ID_MAX_LEN) cid_len = MQTT_CLIENT_ID_MAX_LEN;
    memcpy(config->client_id, &data[offset], cid_len);
    offset += cid_len;

    // ========== P0-1: User ID (필수) ==========
    if (offset >= len) {
        ESP_LOGW(TAG, "user_id missing - v2.1 required field!");
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t uid_len = data[offset++];
    if (offset + uid_len > len) return ESP_ERR_INVALID_ARG;
    if (uid_len > MQTT_USER_ID_MAX_LEN) uid_len = MQTT_USER_ID_MAX_LEN;
    memcpy(config->user_id, &data[offset], uid_len);
    offset += uid_len;

    // ========== P0-1: Device ID (필수) ==========
    if (offset >= len) {
        ESP_LOGW(TAG, "device_id missing - v2.1 required field!");
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t did_len = data[offset++];
    if (offset + did_len > len) return ESP_ERR_INVALID_ARG;
    if (did_len > MQTT_DEVICE_ID_MAX_LEN) did_len = MQTT_DEVICE_ID_MAX_LEN;
    memcpy(config->device_id, &data[offset], did_len);
    offset += did_len;

    // ========== P0-1: Base Topic (필수) ==========
    if (offset >= len) {
        ESP_LOGW(TAG, "base_topic missing - v2.1 required field!");
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t bt_len = data[offset++];
    if (offset + bt_len > len) return ESP_ERR_INVALID_ARG;
    if (bt_len > MQTT_BASE_TOPIC_MAX_LEN) bt_len = MQTT_BASE_TOPIC_MAX_LEN;
    memcpy(config->base_topic, &data[offset], bt_len);
    offset += bt_len;

    // ========== QoS ==========
    if (offset < len) {
        config->qos = data[offset++];
        if (config->qos > 2) config->qos = 1;
    }

    // ========== Use TLS ==========
    if (offset < len) {
        config->use_tls = (data[offset++] != 0);
    }

    // ========== P0-2: Use JWT ==========
    if (offset < len) {
        config->use_jwt = (data[offset++] != 0);
    }

    // Validation: v2.1 필수 필드 확인
    if (strlen(config->user_id) == 0) {
        ESP_LOGE(TAG, "user_id is empty - v2.1 requires user_id!");
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(config->device_id) == 0) {
        ESP_LOGE(TAG, "device_id is empty - v2.1 requires device_id!");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "MQTT config parsed (v2.1):");
    ESP_LOGI(TAG, "  Broker: %s:%d", config->broker, config->port);
    ESP_LOGI(TAG, "  User ID: %s", config->user_id);
    ESP_LOGI(TAG, "  Device ID: %s", config->device_id);
    ESP_LOGI(TAG, "  Base Topic: %s", config->base_topic);
    ESP_LOGI(TAG, "  TLS: %s, JWT: %s", 
             config->use_tls ? "enabled" : "disabled",
             config->use_jwt ? "enabled" : "disabled");
    
    return ESP_OK;
}

/*******************************************************************************
 * UART Configuration Parser (Section 4.3)
 ******************************************************************************/
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

/*******************************************************************************
 * Protocol Configuration Parser (Section 5)
 ******************************************************************************/
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

/*******************************************************************************
 * Data Definition Parser (Section 6) - with safety checks
 ******************************************************************************/
esp_err_t cmd_parse_data_definition(const uint8_t *data, uint16_t len, data_definition_t *def)
{
    ESP_LOGI(TAG, "Parsing data definition: %d bytes", len);
    
    if (len < 2 || !def) {
        ESP_LOGE(TAG, "Invalid args: len=%d, def=%p", len, def);
        return ESP_ERR_INVALID_ARG;
    }

    memset(def, 0, sizeof(data_definition_t));
    def->field_count = data[0];
    def->data_offset = data[1];
    
    ESP_LOGI(TAG, "Field count: %d, data_offset: %d", def->field_count, def->data_offset);

    if (def->field_count > MAX_FIELD_COUNT) {
        ESP_LOGE(TAG, "Too many fields: %d (max %d)", def->field_count, MAX_FIELD_COUNT);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (def->field_count == 0) {
        ESP_LOGW(TAG, "No fields defined");
        return ESP_OK;
    }

    uint16_t offset = 2;
    uint16_t expected_size = 2 + (def->field_count * sizeof(field_definition_t));
    
    ESP_LOGI(TAG, "Expected min size: %d, actual: %d, field_def_size: %d", 
             expected_size, len, sizeof(field_definition_t));

    // Parse field definitions
    for (uint8_t i = 0; i < def->field_count; i++) {
        if (offset + sizeof(field_definition_t) > len) {
            ESP_LOGE(TAG, "Buffer overflow at field %d: offset=%d, need=%d, have=%d",
                     i, offset, sizeof(field_definition_t), len - offset);
            def->field_count = i;  // Truncate to safely parsed fields
            break;
        }
        memcpy(&def->fields[i], &data[offset], sizeof(field_definition_t));
        ESP_LOGI(TAG, "  Field[%d]: type=0x%02X, offset=%d, scale=%d",
                 i, def->fields[i].field_type, def->fields[i].start_offset,
                 def->fields[i].scale_factor);
        offset += sizeof(field_definition_t);
    }

    // Parse field names
    if (offset < len) {
        def->names_length = len - offset;
        if (def->names_length > MAX_FIELD_NAMES_SIZE) {
            ESP_LOGW(TAG, "Names too long: %d, truncating to %d", 
                     def->names_length, MAX_FIELD_NAMES_SIZE);
            def->names_length = MAX_FIELD_NAMES_SIZE;
        }
        memcpy(def->field_names, &data[offset], def->names_length);
        ESP_LOGI(TAG, "Parsed %d bytes of field names", def->names_length);
    }

    ESP_LOGI(TAG, "Data definition parsed: %d fields, data_offset=%d", 
             def->field_count, def->data_offset);
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
 * MQTT Restart Task (non-blocking)
 ******************************************************************************/
static void mqtt_restart_task(void *arg)
{
    mqtt_config_data_t *config = (mqtt_config_data_t *)arg;
    
    // Wait for WiFi connection if not connected
    int retry = 0;
    while (!wifi_manager_is_connected() && retry < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }
    
    if (wifi_manager_is_connected()) {
        mqtt_handler_stop();
        vTaskDelay(pdMS_TO_TICKS(500));
        mqtt_handler_start(config);
    }
    
    vTaskDelete(NULL);
}

/*******************************************************************************
 * Main Command Handler
 ******************************************************************************/
void cmd_handler_process(cmd_code_t cmd, const uint8_t *data, uint16_t len)
{
    esp_err_t ret = ESP_OK;
    result_code_t result = RESULT_SUCCESS;

    ESP_LOGI(TAG, "Processing command: 0x%02X (len=%d)", cmd, len);

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
                // Non-blocking MQTT restart
                xTaskCreate(mqtt_restart_task, "mqtt_restart", 4096, &g_mqtt_config, 3, NULL);
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
                // Dynamic pipeline reconfiguration (특허 핵심 기능)
                uart_handler_update_protocol(&g_protocol_config);
            }
            break;

        case CMD_SET_DATA_DEF:
            ESP_LOGI(TAG, "==> CMD_SET_DATA_DEF received, len=%d", len);
            ret = cmd_parse_data_definition(data, len, &g_data_definition);
            ESP_LOGI(TAG, "==> cmd_parse_data_definition returned %d", ret);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "==> Saving to NVS...");
                nvs_save_data_definition(&g_data_definition);
                ESP_LOGI(TAG, "==> Updating parser...");
                // Dynamic field definition update (특허 핵심 기능)
                data_parser_set_definition(&g_data_definition);
                ESP_LOGI(TAG, "==> CMD_SET_DATA_DEF complete");
            }
            break;

        case CMD_GET_STATUS:
            ble_service_notify_status(&g_device_status);
            break;

        case CMD_SAVE_CONFIG:
            ESP_LOGI(TAG, "All configurations saved to NVS");
            break;

        case CMD_RESET_CONFIG:
            ESP_LOGW(TAG, "Factory reset requested");
            nvs_reset_to_defaults();
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
            break;

        case CMD_START_MONITOR:
            ESP_LOGI(TAG, "Monitoring started");
            // TODO: Set monitoring flag
            break;

        case CMD_STOP_MONITOR:
            ESP_LOGI(TAG, "Monitoring stopped");
            // TODO: Clear monitoring flag
            break;

        case CMD_REQUEST_SYNC:
            ESP_LOGI(TAG, "Config sync requested via BLE");
            if (mqtt_handler_is_connected()) {
                mqtt_handler_request_config_sync();
            } else {
                ret = ESP_ERR_INVALID_STATE;
            }
            break;

        default:
            ESP_LOGW(TAG, "Unknown command: 0x%02X", cmd);
            ret = ESP_ERR_INVALID_ARG;
            break;
    }

    if (ret != ESP_OK) {
        result = (ret == ESP_ERR_INVALID_ARG) ? RESULT_INVALID : RESULT_FAILED;
        ESP_LOGE(TAG, "Command 0x%02X failed with result %d", cmd, result);
    }
    
    ble_service_send_ack(cmd, result);
}

/*******************************************************************************
 * Remote Command Handler (P0-3: MQTT 원격 명령 처리)
 ******************************************************************************/
void cmd_handler_process_remote(const mqtt_remote_command_t *cmd, cJSON *payload)
{
    if (!cmd) return;
    
    ESP_LOGI(TAG, "Processing remote command: %d (request_id=%s)", 
             cmd->command, cmd->request_id);
    
    switch (cmd->command) {
        case MQTT_CMD_UPDATE_CONFIG:
            if (payload) {
                bool config_updated = false;
                
                // payload에서 설정 추출 및 적용
                cJSON *uart = cJSON_GetObjectItem(payload, "uart");
                if (uart) {
                    // UART 설정 업데이트
                    cJSON *baudrate = cJSON_GetObjectItem(uart, "baudrate");
                    if (baudrate && cJSON_IsNumber(baudrate)) {
                        g_uart_config.baudrate = (uint32_t)baudrate->valuedouble;
                    }
                    cJSON *data_bits = cJSON_GetObjectItem(uart, "dataBits");
                    if (data_bits && cJSON_IsNumber(data_bits)) {
                        g_uart_config.data_bits = (uint8_t)data_bits->valuedouble;
                    }
                    cJSON *parity = cJSON_GetObjectItem(uart, "parity");
                    if (parity && cJSON_IsNumber(parity)) {
                        g_uart_config.parity = (uint8_t)parity->valuedouble;
                    }
                    cJSON *stop_bits = cJSON_GetObjectItem(uart, "stopBits");
                    if (stop_bits && cJSON_IsNumber(stop_bits)) {
                        g_uart_config.stop_bits = (uint8_t)stop_bits->valuedouble;
                    }
                    nvs_save_uart_config(&g_uart_config);
                    uart_handler_stop();
                    uart_handler_start(&g_uart_config, &g_protocol_config);
                    ESP_LOGI(TAG, "UART config updated remotely");
                    config_updated = true;
                }
                
                cJSON *protocol = cJSON_GetObjectItem(payload, "protocol");
                if (protocol) {
                    // Protocol 설정 업데이트
                    cJSON *frame_length = cJSON_GetObjectItem(protocol, "frameLength");
                    if (frame_length && cJSON_IsNumber(frame_length)) {
                        g_protocol_config.config.custom.frame_length = (uint16_t)frame_length->valuedouble;
                    }
                    cJSON *stx_enable = cJSON_GetObjectItem(protocol, "stxEnable");
                    if (stx_enable && cJSON_IsBool(stx_enable)) {
                        g_protocol_config.config.custom.stx_enable = cJSON_IsTrue(stx_enable);
                    }
                    cJSON *stx_value = cJSON_GetObjectItem(protocol, "stxValue");
                    if (stx_value && cJSON_IsNumber(stx_value)) {
                        g_protocol_config.config.custom.stx_value = (uint16_t)stx_value->valuedouble;
                    }
                    cJSON *etx_enable = cJSON_GetObjectItem(protocol, "etxEnable");
                    if (etx_enable && cJSON_IsBool(etx_enable)) {
                        g_protocol_config.config.custom.etx_enable = cJSON_IsTrue(etx_enable);
                    }
                    cJSON *etx_value = cJSON_GetObjectItem(protocol, "etxValue");
                    if (etx_value && cJSON_IsNumber(etx_value)) {
                        g_protocol_config.config.custom.etx_value = (uint16_t)etx_value->valuedouble;
                    }
                    cJSON *crc_type = cJSON_GetObjectItem(protocol, "crcType");
                    if (crc_type && cJSON_IsNumber(crc_type)) {
                        g_protocol_config.config.custom.crc_type = (crc_type_t)crc_type->valuedouble;
                    }
                    nvs_save_protocol_config(&g_protocol_config);
                    uart_handler_update_protocol(&g_protocol_config);
                    ESP_LOGI(TAG, "Protocol config updated remotely");
                    config_updated = true;
                }
                
                // 설정 업데이트 응답 전송
                if (config_updated) {
                    mqtt_handler_send_command_response(cmd->request_id, true, "Config updated");
                } else {
                    mqtt_handler_send_command_response(cmd->request_id, false, "No valid config in payload");
                }
            } else {
                mqtt_handler_send_command_response(cmd->request_id, false, "Missing payload");
            }
            break;
            
        case MQTT_CMD_RESTART:
            ESP_LOGW(TAG, "Remote restart requested");
            mqtt_handler_send_command_response(cmd->request_id, true, "Restarting...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
            break;
            
        case MQTT_CMD_REQUEST_STATUS:
            ESP_LOGI(TAG, "Remote status request");
            if (mqtt_handler_is_connected()) {
                extern char g_device_id[32];
                mqtt_handler_publish_status(g_device_id, &g_device_status);
                mqtt_handler_send_command_response(cmd->request_id, true, "Status published");
            } else {
                mqtt_handler_send_command_response(cmd->request_id, false, "MQTT not connected");
            }
            break;
            
        case MQTT_CMD_START_MONITOR:
            ESP_LOGI(TAG, "Remote monitoring start");
            // TODO: Start monitoring
            mqtt_handler_send_command_response(cmd->request_id, true, "Monitoring started");
            break;
            
        case MQTT_CMD_STOP_MONITOR:
            ESP_LOGI(TAG, "Remote monitoring stop");
            // TODO: Stop monitoring
            mqtt_handler_send_command_response(cmd->request_id, true, "Monitoring stopped");
            break;
            
        case MQTT_CMD_FACTORY_RESET:
            ESP_LOGW(TAG, "Remote factory reset requested");
            mqtt_handler_send_command_response(cmd->request_id, true, "Factory resetting...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            nvs_reset_to_defaults();
            esp_restart();
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown remote command: %d", cmd->command);
            mqtt_handler_send_command_response(cmd->request_id, false, "Unknown command");
            break;
    }
}
